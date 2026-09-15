#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "http_upload.h"
#include "manual_clock.h"

#define TAG                       "http_upload"
#define UPLOAD_RETRY              3
#define UPLOAD_RETRY_PAUSE_MS     30000
#define UPLOAD_HTTP_TIMEOUT_MS    30000
#define UPLOAD_TASK_STACK         7168
#define UPLOAD_TASK_PRIO          4
#define OUTBOX_FILE               "/spiffs/upload_outbox.bin"
#define OUTBOX_TEMP_FILE          "/spiffs/upload_outbox.tmp"
#define OUTBOX_BACKUP_FILE        "/spiffs/upload_outbox.bak"
#define OUTBOX_MAGIC              0x4154544EU
#define OUTBOX_VERSION            1U
#define OUTBOX_MAX_RECORDS        500
#define JSON_BUFFER_SIZE          768

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    upload_record_t record;
} stored_record_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool ok;
} json_writer_t;

typedef struct {
    char data[512];
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
static char s_gas_url[384];
static char s_api_token[128];
static int s_pending_count;
static bool s_initialized;

extern const uint8_t cdp_ca_bundle_pem_start[] asm("_binary_cdp_ca_bundle_pem_start");
extern const uint8_t cdp_ca_bundle_pem_end[] asm("_binary_cdp_ca_bundle_pem_end");

static void json_append_char(json_writer_t *writer, char value)
{
    if (!writer->ok || writer->length + 1 >= writer->capacity) {
        writer->ok = false;
        return;
    }
    writer->buffer[writer->length++] = value;
    writer->buffer[writer->length] = '\0';
}

static void json_append_text(json_writer_t *writer, const char *text)
{
    while (*text != '\0' && writer->ok) {
        json_append_char(writer, *text++);
    }
}

static void json_append_string(json_writer_t *writer, const char *value)
{
    static const char hex[] = "0123456789abcdef";
    json_append_char(writer, '"');
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0' && writer->ok; p++) {
        switch (*p) {
        case '"': json_append_text(writer, "\\\""); break;
        case '\\': json_append_text(writer, "\\\\"); break;
        case '\b': json_append_text(writer, "\\b"); break;
        case '\f': json_append_text(writer, "\\f"); break;
        case '\n': json_append_text(writer, "\\n"); break;
        case '\r': json_append_text(writer, "\\r"); break;
        case '\t': json_append_text(writer, "\\t"); break;
        default:
            if (*p < 0x20) {
                json_append_text(writer, "\\u00");
                json_append_char(writer, hex[*p >> 4]);
                json_append_char(writer, hex[*p & 0x0f]);
            } else {
                json_append_char(writer, (char)*p);
            }
        }
    }
    json_append_char(writer, '"');
}

static bool build_json(const upload_record_t *record, char *buffer, size_t buffer_size)
{
    char number[32];
    json_writer_t writer = {
        .buffer = buffer,
        .capacity = buffer_size,
        .length = 0,
        .ok = buffer_size > 0,
    };
    if (buffer_size > 0) buffer[0] = '\0';

    json_append_text(&writer, "{\"event_id\":");
    json_append_string(&writer, record->event_id);
    json_append_text(&writer, ",\"device_id\":");
    json_append_string(&writer, record->device_id);
    json_append_text(&writer, ",\"token\":");
    json_append_string(&writer, s_api_token);
    snprintf(number, sizeof(number), ",\"student_id\":%u", (unsigned)record->student_id);
    json_append_text(&writer, number);
    json_append_text(&writer, ",\"name\":");
    json_append_string(&writer, record->name);
    json_append_text(&writer, ",\"class\":");
    json_append_string(&writer, record->class_name);
    json_append_text(&writer, ",\"timestamp\":");
    json_append_string(&writer, record->timestamp);
    snprintf(number, sizeof(number), ",\"confidence_milli\":%d", record->confidence_milli);
    json_append_text(&writer, number);
    json_append_text(&writer, ",\"method\":\"FACE\"}");
    return writer.ok;
}

static bool stored_record_valid(const stored_record_t *stored)
{
    return stored->magic == OUTBOX_MAGIC && stored->version == OUTBOX_VERSION &&
           stored->record_size == sizeof(upload_record_t) &&
           stored->record.event_id[0] != '\0';
}

static esp_err_t sync_and_close(FILE *file)
{
    esp_err_t result = ESP_OK;
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) result = ESP_FAIL;
    if (fclose(file) != 0) result = ESP_FAIL;
    return result;
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
    stored_record_t stored;
    int count = 0;
    while (fread(&stored, sizeof(stored), 1, file) == 1) {
        if (stored_record_valid(&stored)) count++;
    }
    fclose(file);
    return count;
}

static esp_err_t outbox_repair(void)
{
    FILE *source = fopen(OUTBOX_FILE, "rb");
    if (source == NULL) return ESP_OK;

    bool needs_repair = false;
    if (fseek(source, 0, SEEK_END) != 0) {
        fclose(source);
        return ESP_FAIL;
    }
    const long file_size = ftell(source);
    rewind(source);
    if (file_size < 0 || file_size % (long)sizeof(stored_record_t) != 0) {
        needs_repair = true;
    }

    stored_record_t stored;
    while (fread(&stored, sizeof(stored), 1, source) == 1) {
        if (!stored_record_valid(&stored)) needs_repair = true;
    }
    if (!needs_repair) {
        fclose(source);
        return ESP_OK;
    }

    rewind(source);
    FILE *target = fopen(OUTBOX_TEMP_FILE, "wb");
    if (target == NULL) {
        fclose(source);
        return ESP_FAIL;
    }
    while (fread(&stored, sizeof(stored), 1, source) == 1) {
        if (stored_record_valid(&stored) && fwrite(&stored, sizeof(stored), 1, target) != 1) {
            fclose(source);
            fclose(target);
            remove(OUTBOX_TEMP_FILE);
            return ESP_FAIL;
        }
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
    ESP_LOGW(TAG, "Repaired an interrupted/corrupt upload outbox");
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
    stored_record_t stored = {
        .magic = OUTBOX_MAGIC,
        .version = OUTBOX_VERSION,
        .record_size = sizeof(upload_record_t),
        .record = *record,
    };
    if (fwrite(&stored, sizeof(stored), 1, file) != 1) {
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
    bool found = false;
    stored_record_t stored;
    while (file != NULL && fread(&stored, sizeof(stored), 1, file) == 1) {
        if (stored_record_valid(&stored)) {
            *record = stored.record;
            found = true;
            break;
        }
    }
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

    stored_record_t stored;
    bool removed = false;
    while (fread(&stored, sizeof(stored), 1, source) == 1) {
        if (!removed && stored_record_valid(&stored)) {
            if (strcmp(stored.record.event_id, expected_event_id) != 0) {
                ESP_LOGE(TAG, "Outbox head changed unexpectedly");
                goto done;
            }
            removed = true;
            continue;
        }
        if (stored_record_valid(&stored) && fwrite(&stored, sizeof(stored), 1, target) != 1) {
            goto done;
        }
    }
    fclose(source);
    source = NULL;
    if (!removed) {
        goto done;
    }
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
        size_t available = sizeof(capture->data) - capture->length - 1;
        size_t copy_length = (size_t)event->data_len < available ?
                             (size_t)event->data_len : available;
        if (copy_length > 0) {
            memcpy(capture->data + capture->length, event->data, copy_length);
            capture->length += copy_length;
            capture->data[capture->length] = '\0';
        }
        if (copy_length < (size_t)event->data_len) capture->truncated = true;
        break;

    default:
        break;
    }
    return ESP_OK;
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

static upload_result_t do_upload(const upload_record_t *record)
{
    char json[JSON_BUFFER_SIZE];
    if (!build_json(record, json, sizeof(json))) {
        ESP_LOGE(TAG, "JSON build failed for event=%s", record->event_id);
        return UPLOAD_DEFERRED;
    }

    response_capture_t response = {0};
    esp_http_client_config_t config = {
        .url = s_gas_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = UPLOAD_HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .max_redirection_count = 5,
        .cert_pem = (const char *)cdp_ca_bundle_pem_start,
        .cert_len = (size_t)(cdp_ca_bundle_pem_end - cdp_ca_bundle_pem_start),
        .event_handler = http_event_handler,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return UPLOAD_RETRYABLE;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t error = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (error == ESP_OK && status >= 200 && status < 300 &&
        response_accepts_event(&response, record->event_id)) {
        ESP_LOGI(TAG, "Uploaded event=%s student_id=%u redirects=%u",
                 record->event_id, record->student_id, response.redirect_count);
        return UPLOAD_ACCEPTED;
    }

    ESP_LOGW(TAG,
             "Upload failed: event=%s err=%s status=%d redirects=%u json=%d truncated=%d response='%.160s'",
             record->event_id, esp_err_to_name(error), status, response.redirect_count,
             response.is_json, response.truncated, response.data);
    if (response.redirect_rejected) return UPLOAD_DEFERRED;
    if (error != ESP_OK || status == 408 || status == 429 || status >= 500 ||
        strstr(response.data, "\"retryable\":true") != NULL) {
        return UPLOAD_RETRYABLE;
    }
    return UPLOAD_DEFERRED;
}

static void upload_task(void *arg)
{
    (void)arg;
    upload_record_t record;
    uint8_t wake_signal;
    bool clock_wait_logged = false;
    while (true) {
        if (!manual_clock_is_valid()) {
            if (!clock_wait_logged) {
                ESP_LOGW(TAG, "Upload paused until Wi-Fi/NTP clock is valid");
                clock_wait_logged = true;
            }
            xQueueReceive(s_wake_queue, &wake_signal, pdMS_TO_TICKS(5000));
            continue;
        }
        if (clock_wait_logged) {
            ESP_LOGI(TAG, "Clock is valid; resuming pending uploads");
            clock_wait_logged = false;
        }
        while (outbox_peek(&record)) {
            upload_result_t result = UPLOAD_RETRYABLE;
            for (int attempt = 0; attempt < UPLOAD_RETRY &&
                                  result == UPLOAD_RETRYABLE; attempt++) {
                if (attempt > 0) vTaskDelay(pdMS_TO_TICKS(2000 * attempt));
                result = do_upload(&record);
            }
            if (result != UPLOAD_ACCEPTED) break;
            if (outbox_remove_first(record.event_id) != ESP_OK) {
                ESP_LOGE(TAG, "Could not acknowledge event=%s", record.event_id);
                break;
            }
        }
        xQueueReceive(s_wake_queue, &wake_signal, pdMS_TO_TICKS(UPLOAD_RETRY_PAUSE_MS));
    }
}

esp_err_t http_upload_init(const char *gas_url, const char *api_token)
{
    if (s_initialized) return ESP_OK;
    if (gas_url == NULL || strncmp(gas_url, "https://", 8) != 0 ||
        strlen(gas_url) >= sizeof(s_gas_url)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (api_token == NULL || strlen(api_token) < 32 ||
        strlen(api_token) >= sizeof(s_api_token)) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_gas_url, sizeof(s_gas_url), "%s", gas_url);
    snprintf(s_api_token, sizeof(s_api_token), "%s", api_token);

    s_outbox_mutex = xSemaphoreCreateMutex();
    s_wake_queue = xQueueCreate(1, sizeof(uint8_t));
    if (s_outbox_mutex == NULL || s_wake_queue == NULL) {
        if (s_outbox_mutex != NULL) vSemaphoreDelete(s_outbox_mutex);
        if (s_wake_queue != NULL) vQueueDelete(s_wake_queue);
        s_outbox_mutex = NULL;
        s_wake_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    recover_interrupted_compaction();
    if (outbox_repair() != ESP_OK) {
        ESP_LOGW(TAG, "Outbox repair could not complete; valid records will still be retried");
    }
    if (xSemaphoreTake(s_outbox_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    s_pending_count = outbox_count_locked();
    xSemaphoreGive(s_outbox_mutex);

    if (xTaskCreate(upload_task, "http_upload", UPLOAD_TASK_STACK, NULL,
                    UPLOAD_TASK_PRIO, NULL) != pdPASS) {
        vSemaphoreDelete(s_outbox_mutex);
        vQueueDelete(s_wake_queue);
        s_outbox_mutex = NULL;
        s_wake_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "Verified TLS CA store ready: %u bytes",
             (unsigned)(cdp_ca_bundle_pem_end - cdp_ca_bundle_pem_start));
    ESP_LOGI(TAG, "Durable uploader ready; %d pending record(s)", s_pending_count);
    return ESP_OK;
}

esp_err_t http_upload_stage(const upload_record_t *record)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (record == NULL || record->event_id[0] == '\0' ||
        record->device_id[0] == '\0' || record->student_id == 0 ||
        record->name[0] == '\0' || record->timestamp[0] == '\0' ||
        record->confidence_milli < 0 || record->confidence_milli > 1000 ||
        memchr(record->event_id, '\0', sizeof(record->event_id)) == NULL ||
        memchr(record->device_id, '\0', sizeof(record->device_id)) == NULL ||
        memchr(record->name, '\0', sizeof(record->name)) == NULL ||
        memchr(record->class_name, '\0', sizeof(record->class_name)) == NULL ||
        memchr(record->timestamp, '\0', sizeof(record->timestamp)) == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = outbox_append(record);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not persist upload event=%s", record->event_id);
    }
    return result;
}

void http_upload_notify(void)
{
    if (!s_initialized || s_wake_queue == NULL) return;
    uint8_t signal = 1;
    xQueueOverwrite(s_wake_queue, &signal);
}

int http_upload_pending_count(void)
{
    if (!s_initialized || xSemaphoreTake(s_outbox_mutex, portMAX_DELAY) != pdTRUE) return 0;
    int count = s_pending_count;
    xSemaphoreGive(s_outbox_mutex);
    return count;
}
