#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "http_payload.h"
#include "http_upload.h"
#include "manual_clock.h"
#include "outbox_codec.h"
#include "upload_policy.h"

#define TAG                       "http_upload"
#define UPLOAD_HTTP_TIMEOUT_MS    30000
#define UPLOAD_TASK_STACK         8192
#define UPLOAD_TASK_PRIO          4
#define UPLOAD_TASK_CORE          1
#define UPLOAD_BATCH_MAX          5
#define UPLOAD_IDLE_POLL_MS       5000
#define HEARTBEAT_INTERVAL_MS     60000
#define OUTBOX_FILE               "/spiffs/upload_outbox.bin"
#define OUTBOX_TEMP_FILE          "/spiffs/upload_outbox.tmp"
#define OUTBOX_BACKUP_FILE        "/spiffs/upload_outbox.bak"
#define OUTBOX_MAX_RECORDS        500
#define JSON_BUFFER_SIZE          768
#define LAST_ERROR_MAX            160

typedef struct {
    char data[1024];
    size_t length;
    int status_code;
    unsigned redirect_count;
    bool is_json;
    bool truncated;
    bool redirect_rejected;
    char redirect_location[384];
} response_capture_t;

typedef enum {
    UPLOAD_ACCEPTED,
    UPLOAD_RETRYABLE,
    UPLOAD_DEFERRED,
} upload_result_t;

static QueueHandle_t s_wake_queue;
static SemaphoreHandle_t s_outbox_mutex;
static SemaphoreHandle_t s_cloud_mutex;
static volatile bool s_cloud_suspended;
static char s_gas_url[384];
static char s_api_token[128];
static char s_device_id[HTTP_UPLOAD_DEVICE_ID_MAX];
static char s_last_error[LAST_ERROR_MAX + 1];
static int s_pending_count;
static uint32_t s_consecutive_failures;
static uint32_t s_pending_failure_count;
static uint32_t s_last_sync_latency_ms;
static bool s_initialized;

extern const uint8_t cdp_ca_bundle_pem_start[] asm("_binary_cdp_ca_bundle_pem_start");
extern const uint8_t cdp_ca_bundle_pem_end[] asm("_binary_cdp_ca_bundle_pem_end");

static esp_err_t sync_and_close(FILE *file)
{
    esp_err_t result = ESP_OK;
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) result = ESP_FAIL;
    if (fclose(file) != 0) result = ESP_FAIL;
    return result;
}

static bool outbox_write_v2(FILE *file, const upload_record_t *record)
{
    uint8_t encoded[OUTBOX_RECORD_V2_SIZE];
    size_t encoded_size = 0;
    return outbox_encode_v2(record, encoded, sizeof(encoded), &encoded_size) &&
           fwrite(encoded, encoded_size, 1, file) == 1;
}

/* Scans forward after an invalid header so one corrupt record cannot block
 * later valid records. V1 records remain readable and are rewritten as V2
 * during the next repair/compaction. */
static bool outbox_read_next(FILE *file, upload_record_t *record,
                             bool *legacy_found, bool *corrupt_found)
{
    while (true) {
        const long start = ftell(file);
        if (start < 0) {
            *corrupt_found = true;
            return false;
        }

        uint8_t encoded[OUTBOX_RECORD_V2_SIZE] = {0};
        const size_t header_read =
            fread(encoded, 1, sizeof(outbox_record_header_t), file);
        if (header_read == 0) return false;
        if (header_read != sizeof(outbox_record_header_t)) {
            *corrupt_found = true;
            return false;
        }

        outbox_record_header_t header;
        memcpy(&header, encoded, sizeof(header));
        const size_t expected = outbox_encoded_size_for_version(header.version);
        if (header.magic != OUTBOX_RECORD_MAGIC || expected == 0 ||
            header.record_size != sizeof(upload_record_t)) {
            *corrupt_found = true;
            if (fseek(file, start + 1, SEEK_SET) != 0) return false;
            continue;
        }

        const size_t remaining = expected - sizeof(header);
        if (fread(encoded + sizeof(header), 1, remaining, file) != remaining) {
            *corrupt_found = true;
            return false;
        }

        size_t consumed = 0;
        bool legacy = false;
        const outbox_decode_result_t result =
            outbox_decode(encoded, expected, record, &consumed, &legacy);
        if (result == OUTBOX_DECODE_OK) {
            if (legacy) *legacy_found = true;
            return true;
        }
        *corrupt_found = true;
    }
}

static void recover_interrupted_compaction(void)
{
    FILE *main_file = fopen(OUTBOX_FILE, "rb");
    const bool main_exists = main_file != NULL;
    if (main_file != NULL) fclose(main_file);

    if (main_exists) {
        remove(OUTBOX_TEMP_FILE);
        remove(OUTBOX_BACKUP_FILE);
        return;
    }
    if (rename(OUTBOX_TEMP_FILE, OUTBOX_FILE) == 0) {
        remove(OUTBOX_BACKUP_FILE);
        return;
    }
    rename(OUTBOX_BACKUP_FILE, OUTBOX_FILE);
}

static int outbox_count_locked(void)
{
    FILE *file = fopen(OUTBOX_FILE, "rb");
    if (file == NULL) return 0;
    upload_record_t record;
    bool legacy_found = false;
    bool corrupt_found = false;
    int count = 0;
    while (outbox_read_next(file, &record, &legacy_found, &corrupt_found)) count++;
    fclose(file);
    return count;
}

static esp_err_t outbox_repair(void)
{
    FILE *source = fopen(OUTBOX_FILE, "rb");
    if (source == NULL) return ESP_OK;

    upload_record_t record;
    bool legacy_found = false;
    bool corrupt_found = false;
    while (outbox_read_next(source, &record, &legacy_found, &corrupt_found)) {}
    if (!legacy_found && !corrupt_found) {
        fclose(source);
        return ESP_OK;
    }

    rewind(source);
    FILE *target = fopen(OUTBOX_TEMP_FILE, "wb");
    if (target == NULL) {
        fclose(source);
        return ESP_FAIL;
    }

    bool ignored_legacy = false;
    bool ignored_corrupt = false;
    int retained = 0;
    while (outbox_read_next(source, &record, &ignored_legacy, &ignored_corrupt)) {
        if (!outbox_write_v2(target, &record)) {
            fclose(source);
            fclose(target);
            remove(OUTBOX_TEMP_FILE);
            return ESP_FAIL;
        }
        retained++;
    }
    fclose(source);
    if (sync_and_close(target) != ESP_OK) {
        remove(OUTBOX_TEMP_FILE);
        return ESP_FAIL;
    }

    remove(OUTBOX_BACKUP_FILE);
    if (rename(OUTBOX_FILE, OUTBOX_BACKUP_FILE) != 0 ||
        rename(OUTBOX_TEMP_FILE, OUTBOX_FILE) != 0) {
        recover_interrupted_compaction();
        return ESP_FAIL;
    }
    remove(OUTBOX_BACKUP_FILE);
    ESP_LOGW(TAG, "Outbox upgraded/repaired: retained=%d legacy=%d corrupt=%d",
             retained, legacy_found, corrupt_found);
    return ESP_OK;
}

static esp_err_t outbox_append(const upload_record_t *record)
{
    if (xSemaphoreTake(s_outbox_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    esp_err_t result = ESP_OK;
    if (s_pending_count >= OUTBOX_MAX_RECORDS) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }

    FILE *file = fopen(OUTBOX_FILE, "ab");
    if (file == NULL) {
        result = ESP_FAIL;
        goto done;
    }
    if (!outbox_write_v2(file, record)) {
        fclose(file);
        result = ESP_FAIL;
        goto done;
    }
    result = sync_and_close(file);
    if (result == ESP_OK) s_pending_count++;

done:
    xSemaphoreGive(s_outbox_mutex);
    return result;
}

static bool outbox_peek(upload_record_t *record)
{
    if (xSemaphoreTake(s_outbox_mutex, portMAX_DELAY) != pdTRUE) return false;
    FILE *file = fopen(OUTBOX_FILE, "rb");
    bool legacy_found = false;
    bool corrupt_found = false;
    const bool found = file != NULL &&
        outbox_read_next(file, record, &legacy_found, &corrupt_found);
    if (file != NULL) fclose(file);
    xSemaphoreGive(s_outbox_mutex);
    return found;
}

static esp_err_t outbox_remove_first(const char *expected_event_id)
{
    if (xSemaphoreTake(s_outbox_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    esp_err_t result = ESP_FAIL;
    FILE *source = fopen(OUTBOX_FILE, "rb");
    FILE *target = NULL;
    if (source == NULL) goto done;
    target = fopen(OUTBOX_TEMP_FILE, "wb");
    if (target == NULL) goto done;

    upload_record_t record;
    bool legacy_found = false;
    bool corrupt_found = false;
    bool removed = false;
    while (outbox_read_next(source, &record, &legacy_found, &corrupt_found)) {
        if (!removed) {
            if (strcmp(record.event_id, expected_event_id) != 0) {
                ESP_LOGE(TAG, "Outbox head changed unexpectedly");
                goto done;
            }
            removed = true;
            continue;
        }
        if (!outbox_write_v2(target, &record)) goto done;
    }
    fclose(source);
    source = NULL;
    if (!removed) goto done;
    if (sync_and_close(target) != ESP_OK) {
        target = NULL;
        goto done;
    }
    target = NULL;

    remove(OUTBOX_BACKUP_FILE);
    if (rename(OUTBOX_FILE, OUTBOX_BACKUP_FILE) != 0 ||
        rename(OUTBOX_TEMP_FILE, OUTBOX_FILE) != 0) {
        recover_interrupted_compaction();
        goto done;
    }
    remove(OUTBOX_BACKUP_FILE);
    s_pending_count--;
    result = ESP_OK;

done:
    if (source != NULL) fclose(source);
    if (target != NULL) fclose(target);
    if (result != ESP_OK) remove(OUTBOX_TEMP_FILE);
    xSemaphoreGive(s_outbox_mutex);
    return result;
}

static bool is_allowed_redirect(const char *url)
{
    static const char expected[] = "https://script.googleusercontent.com/";
    return url != NULL && strncmp(url, expected, sizeof(expected) - 1) == 0;
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    response_capture_t *capture = (response_capture_t *)event->user_data;
    if (capture == NULL) return ESP_OK;

    switch (event->event_id) {
    case HTTP_EVENT_ON_STATUS_CODE:
        capture->length = 0;
        capture->data[0] = '\0';
        capture->is_json = false;
        capture->truncated = false;
        capture->redirect_location[0] = '\0';
        capture->status_code = esp_http_client_get_status_code(event->client);
        break;

    case HTTP_EVENT_ON_HEADER:
        if (event->header_key == NULL || event->header_value == NULL) break;
        if (strcasecmp(event->header_key, "Content-Type") == 0) {
            const char *value = event->header_value;
            while (*value == ' ' || *value == '\t') value++;
            capture->is_json = strncasecmp(value, "application/json", 16) == 0;
        } else if (strcasecmp(event->header_key, "Location") == 0) {
            snprintf(capture->redirect_location, sizeof(capture->redirect_location),
                     "%s", event->header_value);
        }
        break;

    case HTTP_EVENT_REDIRECT: {
        capture->redirect_count++;
        const int status = esp_http_client_get_status_code(event->client);
        if (!is_allowed_redirect(capture->redirect_location)) {
            capture->redirect_rejected = true;
            ESP_LOGE(TAG, "Rejected redirect target (HTTP %d); deploy the Web App for anonymous access",
                     status);
            break;
        }

        esp_err_t error = ESP_OK;
        if (status == 301 || status == 302 || status == 303) {
            error = esp_http_client_set_method(event->client, HTTP_METHOD_GET);
            if (error == ESP_OK) {
                error = esp_http_client_set_post_field(event->client, NULL, 0);
            }
        }
        if (error == ESP_OK) error = esp_http_client_set_redirection(event->client);
        if (error != ESP_OK) {
            capture->redirect_rejected = true;
            ESP_LOGE(TAG, "Could not follow HTTP %d redirect: %s",
                     status, esp_err_to_name(error));
        }
        break;
    }

    case HTTP_EVENT_ON_DATA:
        if (event->data == NULL || event->data_len <= 0) break;
        {
            const size_t available =
                sizeof(capture->data) - capture->length - 1;
            const size_t copy_length = (size_t)event->data_len < available ?
                                       (size_t)event->data_len : available;
            if (copy_length > 0) {
                memcpy(capture->data + capture->length, event->data, copy_length);
                capture->length += copy_length;
                capture->data[capture->length] = '\0';
            }
            if (copy_length < (size_t)event->data_len) capture->truncated = true;
        }
        break;

    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t perform_json_request(const char *url, const char *json,
                                      response_capture_t *response,
                                      int *status, uint32_t *latency_ms)
{
    bool cloud_locked = false;
    if (s_cloud_mutex != NULL) {
        if (xSemaphoreTake(s_cloud_mutex, portMAX_DELAY) != pdTRUE) {
            *status = 0;
            *latency_ms = 0;
            return ESP_ERR_TIMEOUT;
        }
        cloud_locked = true;
    }
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = UPLOAD_HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .max_redirection_count = 5,
        .cert_pem = (const char *)cdp_ca_bundle_pem_start,
        .cert_len = (size_t)(cdp_ca_bundle_pem_end - cdp_ca_bundle_pem_start),
        .event_handler = http_event_handler,
        .user_data = response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        *status = 0;
        *latency_ms = 0;
        if (cloud_locked) xSemaphoreGive(s_cloud_mutex);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));
    const int64_t started = esp_timer_get_time();
    const esp_err_t error = esp_http_client_perform(client);
    const int64_t elapsed_us = esp_timer_get_time() - started;
    *status = esp_http_client_get_status_code(client);
    *latency_ms = elapsed_us <= 0 ? 0U : (uint32_t)(elapsed_us / 1000);
    esp_http_client_cleanup(client);
    if (cloud_locked) xSemaphoreGive(s_cloud_mutex);
    return error;
}

static bool response_accepts_event(const response_capture_t *response,
                                   const char *event_id)
{
    char event_match[HTTP_UPLOAD_EVENT_ID_MAX + 16];
    const int written = snprintf(event_match, sizeof(event_match),
                                 "\"event_id\":\"%s\"", event_id);
    return written > 0 && (size_t)written < sizeof(event_match) &&
           response->is_json && !response->truncated &&
           strstr(response->data, "\"ok\":true") != NULL &&
           strstr(response->data, event_match) != NULL;
}

static bool response_accepts_heartbeat(const response_capture_t *response,
                                       const char *device_id)
{
    char device_match[HTTP_UPLOAD_DEVICE_ID_MAX + 20];
    const int written = snprintf(device_match, sizeof(device_match),
                                 "\"device_id\":\"%s\"", device_id);
    return written > 0 && (size_t)written < sizeof(device_match) &&
           response->is_json && !response->truncated &&
           strstr(response->data, "\"ok\":true") != NULL &&
           strstr(response->data, device_match) != NULL;
}

static upload_result_t classify_failure(esp_err_t error, int status,
                                        const response_capture_t *response)
{
    if (response->redirect_rejected) return UPLOAD_DEFERRED;
    if (error != ESP_OK || status == 408 || status == 429 || status >= 500 ||
        strstr(response->data, "\"retryable\":true") != NULL) {
        return UPLOAD_RETRYABLE;
    }
    return UPLOAD_DEFERRED;
}

static void note_transfer_failure(const char *operation, esp_err_t error,
                                  int status)
{
    if (s_consecutive_failures < UINT32_MAX) s_consecutive_failures++;
    if (s_consecutive_failures > s_pending_failure_count) {
        s_pending_failure_count = s_consecutive_failures;
    }
    snprintf(s_last_error, sizeof(s_last_error), "%s:%s:http_%d",
             operation, esp_err_to_name(error), status);
}

static void note_transfer_success(uint32_t latency_ms, bool heartbeat_reported)
{
    s_last_sync_latency_ms = latency_ms;
    s_consecutive_failures = 0;
    if (heartbeat_reported) {
        s_pending_failure_count = 0;
        s_last_error[0] = '\0';
    }
}

esp_err_t http_upload_test_config(const char *gas_url,
                                  const char *api_token,
                                  const char *device_id,
                                  int rssi)
{
    if (gas_url == NULL || api_token == NULL || device_id == NULL ||
        strncmp(gas_url, "https://", 8) != 0 || strlen(gas_url) >= sizeof(s_gas_url) ||
        strlen(api_token) < 32 || strlen(api_token) >= sizeof(s_api_token) ||
        device_id[0] == '\0' || strlen(device_id) >= HTTP_UPLOAD_DEVICE_ID_MAX ||
        rssi < -127 || rssi > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char json[JSON_BUFFER_SIZE];
    const heartbeat_payload_t heartbeat = {
        .device_id = device_id,
        .api_token = api_token,
        .uptime_seconds = 0,
        .rssi = rssi,
        .free_heap = 0,
        .free_psram = 0,
        .pending_events = 0,
        .sync_latency_ms = 0,
        .consecutive_failures = 0,
        .last_error = "",
    };
    if (!http_payload_build_heartbeat(&heartbeat, json, sizeof(json))) {
        return ESP_ERR_INVALID_ARG;
    }

    response_capture_t response = {0};
    int status = 0;
    uint32_t latency_ms = 0;
    const esp_err_t error = perform_json_request(
        gas_url, json, &response, &status, &latency_ms);
    if (error == ESP_OK && status >= 200 && status < 300 &&
        response_accepts_heartbeat(&response, device_id)) {
        ESP_LOGI(TAG, "Provisioning cloud authentication probe accepted");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Provisioning cloud probe failed: err=%s status=%d redirects=%u",
             esp_err_to_name(error), status, response.redirect_count);
    return error == ESP_OK ? ESP_FAIL : error;
}

static upload_result_t do_upload(const upload_record_t *record)
{
    char json[JSON_BUFFER_SIZE];
    if (!http_payload_build_attendance(record, s_api_token, json, sizeof(json))) {
        ESP_LOGE(TAG, "JSON build failed for event=%s", record->event_id);
        note_transfer_failure("attendance_json", ESP_ERR_INVALID_ARG, 0);
        return UPLOAD_DEFERRED;
    }

    response_capture_t response = {0};
    int status = 0;
    uint32_t latency_ms = 0;
    const esp_err_t error = perform_json_request(
        s_gas_url, json, &response, &status, &latency_ms);
    if (error == ESP_OK && status >= 200 && status < 300 &&
        response_accepts_event(&response, record->event_id)) {
        note_transfer_success(latency_ms, false);
        ESP_LOGI(TAG, "Uploaded event=%s student_id=%u redirects=%u latency=%ums",
                 record->event_id, record->student_id, response.redirect_count,
                 (unsigned)latency_ms);
        return UPLOAD_ACCEPTED;
    }

    note_transfer_failure("attendance", error, status);
    ESP_LOGW(TAG,
             "Upload failed: event=%s err=%s status=%d redirects=%u json=%d truncated=%d response='%.160s'",
             record->event_id, esp_err_to_name(error), status, response.redirect_count,
             response.is_json, response.truncated, response.data);
    return classify_failure(error, status, &response);
}

static int current_rssi(void)
{
    wifi_ap_record_t access_point = {0};
    return esp_wifi_sta_get_ap_info(&access_point) == ESP_OK ?
           access_point.rssi : -127;
}

static upload_result_t do_heartbeat(void)
{
    char json[JSON_BUFFER_SIZE];
    const heartbeat_payload_t heartbeat = {
        .device_id = s_device_id,
        .api_token = s_api_token,
        .uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000),
        .rssi = current_rssi(),
        .free_heap = (uint32_t)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .free_psram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        .pending_events = (uint32_t)http_upload_pending_count(),
        .sync_latency_ms = s_last_sync_latency_ms,
        .consecutive_failures = s_pending_failure_count,
        .last_error = s_last_error,
    };
    if (!http_payload_build_heartbeat(&heartbeat, json, sizeof(json))) {
        note_transfer_failure("heartbeat_json", ESP_ERR_INVALID_ARG, 0);
        return UPLOAD_DEFERRED;
    }

    response_capture_t response = {0};
    int status = 0;
    uint32_t latency_ms = 0;
    const esp_err_t error = perform_json_request(
        s_gas_url, json, &response, &status, &latency_ms);
    if (error == ESP_OK && status >= 200 && status < 300 &&
        response_accepts_heartbeat(&response, s_device_id)) {
        note_transfer_success(latency_ms, true);
        ESP_LOGI(TAG,
                 "Heartbeat accepted: rssi=%d heap=%u psram=%u pending=%lu latency=%ums",
                 heartbeat.rssi, (unsigned)heartbeat.free_heap,
                 (unsigned)heartbeat.free_psram,
                 (unsigned long)heartbeat.pending_events,
                 (unsigned)latency_ms);
        return UPLOAD_ACCEPTED;
    }

    note_transfer_failure("heartbeat", error, status);
    ESP_LOGW(TAG,
             "Heartbeat failed: err=%s status=%d redirects=%u json=%d truncated=%d",
             esp_err_to_name(error), status, response.redirect_count,
             response.is_json, response.truncated);
    return classify_failure(error, status, &response);
}

static bool tick_due(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void wait_after_failure(void)
{
    const uint32_t delay_ms =
        upload_policy_backoff_ms(s_consecutive_failures, esp_random());
    ESP_LOGW(TAG, "Cloud backoff: failures=%lu next_retry=%ums",
             (unsigned long)s_consecutive_failures, (unsigned)delay_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static void upload_task(void *arg)
{
    (void)arg;
    upload_record_t record;
    uint8_t wake_signal;
    bool clock_wait_logged = false;
    TickType_t heartbeat_deadline = xTaskGetTickCount();

    while (true) {
        if (s_cloud_suspended) {
            xQueueReceive(s_wake_queue, &wake_signal, pdMS_TO_TICKS(1000));
            continue;
        }
        if (!manual_clock_is_valid()) {
            if (!clock_wait_logged) {
                ESP_LOGW(TAG, "Upload paused until Wi-Fi/NTP clock is valid");
                clock_wait_logged = true;
            }
            xQueueReceive(s_wake_queue, &wake_signal, pdMS_TO_TICKS(5000));
            continue;
        }
        if (clock_wait_logged) {
            ESP_LOGI(TAG, "Clock is valid; resuming cloud synchronization");
            clock_wait_logged = false;
        }

        bool transfer_failed = false;
        int processed = 0;
        while (!s_cloud_suspended && processed < UPLOAD_BATCH_MAX &&
               outbox_peek(&record)) {
            const upload_result_t result = do_upload(&record);
            if (result != UPLOAD_ACCEPTED) {
                transfer_failed = true;
                break;
            }
            if (outbox_remove_first(record.event_id) != ESP_OK) {
                ESP_LOGE(TAG, "Could not acknowledge event=%s", record.event_id);
                note_transfer_failure("outbox_ack", ESP_FAIL, 0);
                transfer_failed = true;
                break;
            }
            processed++;
        }

        if (transfer_failed) {
            wait_after_failure();
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if (tick_due(now, heartbeat_deadline)) {
            const upload_result_t heartbeat_result = do_heartbeat();
            if (heartbeat_result != UPLOAD_ACCEPTED) {
                wait_after_failure();
                continue;
            }
            heartbeat_deadline =
                xTaskGetTickCount() + pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS);
        }

        if (http_upload_pending_count() > 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        now = xTaskGetTickCount();
        TickType_t wait_ticks = pdMS_TO_TICKS(UPLOAD_IDLE_POLL_MS);
        if (!tick_due(now, heartbeat_deadline)) {
            const TickType_t until_heartbeat = heartbeat_deadline - now;
            if (until_heartbeat < wait_ticks) wait_ticks = until_heartbeat;
        } else {
            wait_ticks = 0;
        }
        xQueueReceive(s_wake_queue, &wake_signal, wait_ticks);
    }
}

static bool valid_device_id(const char *device_id)
{
    if (device_id == NULL || device_id[0] == '\0' ||
        strlen(device_id) >= sizeof(s_device_id)) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)device_id;
         *cursor != '\0'; cursor++) {
        const bool allowed = (*cursor >= 'a' && *cursor <= 'z') ||
                             (*cursor >= 'A' && *cursor <= 'Z') ||
                             (*cursor >= '0' && *cursor <= '9') ||
                             *cursor == '.' || *cursor == '_' ||
                             *cursor == ':' || *cursor == '-';
        if (!allowed) return false;
    }
    return true;
}

esp_err_t http_upload_init(const char *gas_url, const char *api_token,
                           const char *device_id)
{
    if (s_initialized) return ESP_OK;
    if (gas_url == NULL || strncmp(gas_url, "https://", 8) != 0 ||
        strlen(gas_url) >= sizeof(s_gas_url)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (api_token == NULL || strlen(api_token) < 32 ||
        strlen(api_token) >= sizeof(s_api_token) || !valid_device_id(device_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_gas_url, sizeof(s_gas_url), "%s", gas_url);
    snprintf(s_api_token, sizeof(s_api_token), "%s", api_token);
    snprintf(s_device_id, sizeof(s_device_id), "%s", device_id);
    s_last_error[0] = '\0';
    s_consecutive_failures = 0;
    s_pending_failure_count = 0;
    s_last_sync_latency_ms = 0;

    s_outbox_mutex = xSemaphoreCreateMutex();
    s_cloud_mutex = xSemaphoreCreateMutex();
    s_wake_queue = xQueueCreate(1, sizeof(uint8_t));
    if (s_outbox_mutex == NULL || s_cloud_mutex == NULL || s_wake_queue == NULL) {
        if (s_outbox_mutex != NULL) vSemaphoreDelete(s_outbox_mutex);
        if (s_cloud_mutex != NULL) vSemaphoreDelete(s_cloud_mutex);
        if (s_wake_queue != NULL) vQueueDelete(s_wake_queue);
        s_outbox_mutex = NULL;
        s_cloud_mutex = NULL;
        s_wake_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_cloud_suspended = false;

    recover_interrupted_compaction();
    if (outbox_repair() != ESP_OK) {
        ESP_LOGW(TAG, "Outbox repair could not complete; valid records will still be retried");
        snprintf(s_last_error, sizeof(s_last_error), "outbox_repair_failed");
    }
    if (xSemaphoreTake(s_outbox_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    s_pending_count = outbox_count_locked();
    xSemaphoreGive(s_outbox_mutex);

    /* Main face recognition is pinned to core 0. Keep TLS/JSON work on core 1
     * so a slow redirect or retry cannot starve the camera consumer. */
    s_initialized = true;
    if (xTaskCreatePinnedToCore(upload_task, "http_upload", UPLOAD_TASK_STACK,
                    NULL, UPLOAD_TASK_PRIO, NULL, UPLOAD_TASK_CORE) != pdPASS) {
        s_initialized = false;
        vSemaphoreDelete(s_outbox_mutex);
        vSemaphoreDelete(s_cloud_mutex);
        vQueueDelete(s_wake_queue);
        s_outbox_mutex = NULL;
        s_cloud_mutex = NULL;
        s_wake_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Verified TLS CA store ready: %u bytes",
             (unsigned)(cdp_ca_bundle_pem_end - cdp_ca_bundle_pem_start));
    ESP_LOGI(TAG,
             "P2.4 uploader ready: pending=%d heartbeat=%ds outbox=v%u",
             s_pending_count, HEARTBEAT_INTERVAL_MS / 1000,
             OUTBOX_RECORD_VERSION_V2);
    return ESP_OK;
}

esp_err_t http_upload_fetch_ota_manifest(char *output, size_t output_size)
{
    if (!s_initialized || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    char json[JSON_BUFFER_SIZE];
    if (!http_payload_build_ota_manifest_request(
            s_device_id, s_api_token, json, sizeof(json))) {
        return ESP_ERR_INVALID_SIZE;
    }
    response_capture_t response = {0};
    int status = 0;
    uint32_t latency_ms = 0;
    const esp_err_t error = perform_json_request(
        s_gas_url, json, &response, &status, &latency_ms);
    if (error != ESP_OK || status < 200 || status >= 300 ||
        !response.is_json || response.truncated ||
        response.length + 1 > output_size) {
        ESP_LOGW(TAG,
                 "OTA manifest failed: err=%s status=%d redirects=%u json=%d truncated=%d",
                 esp_err_to_name(error), status, response.redirect_count,
                 response.is_json, response.truncated);
        return error == ESP_OK ? ESP_FAIL : error;
    }
    memcpy(output, response.data, response.length + 1);
    note_transfer_success(latency_ms, false);
    return ESP_OK;
}

esp_err_t http_upload_suspend_for_ota(void)
{
    if (!s_initialized || s_cloud_mutex == NULL) return ESP_ERR_INVALID_STATE;
    s_cloud_suspended = true;
    http_upload_notify();
    if (xSemaphoreTake(s_cloud_mutex, portMAX_DELAY) != pdTRUE) {
        s_cloud_suspended = false;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void http_upload_resume_after_ota(void)
{
    if (!s_initialized || s_cloud_mutex == NULL || !s_cloud_suspended) return;
    xSemaphoreGive(s_cloud_mutex);
    s_cloud_suspended = false;
    http_upload_notify();
}

esp_err_t http_upload_stage(const upload_record_t *record)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!outbox_upload_record_valid(record)) return ESP_ERR_INVALID_ARG;
    const esp_err_t result = outbox_append(record);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not persist upload event=%s",
                 record == NULL ? "(null)" : record->event_id);
    }
    return result;
}

void http_upload_notify(void)
{
    if (!s_initialized || s_wake_queue == NULL) return;
    const uint8_t signal = 1;
    xQueueOverwrite(s_wake_queue, &signal);
}

int http_upload_pending_count(void)
{
    if (!s_initialized || s_outbox_mutex == NULL ||
        xSemaphoreTake(s_outbox_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    const int count = s_pending_count;
    xSemaphoreGive(s_outbox_mutex);
    return count;
}
