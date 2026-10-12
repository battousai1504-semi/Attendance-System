#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "audio_feedback.h"
#include "device_config.h"
#include "enroll_server.h"
#include "face_engine.h"
#include "http_upload.h"
#include "student_db.h"

#define TAG "enroll_server"
#define ENROLLMENT_CAPACITY STUDENT_MAX_COUNT
#define PROVISION_GOT_IP_BIT       BIT0
#define PROVISION_DISCONNECTED_BIT BIT1
#define PROVISION_WIFI_TIMEOUT_MS  20000
#define PROVISION_NTP_TIMEOUT_MS   20000
#define WIFI_SCAN_LIMIT            16
#define CONFIG_JOB_TASK_STACK      8192
#define CONFIG_JOB_TASK_PRIORITY   4

static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static volatile bool s_done_requested;
static volatile bool s_restart_requested;
static volatile bool s_candidate_connecting;
static bool s_wifi_initialized;
static bool s_wifi_started;
static EventGroupHandle_t s_provisioning_events;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static TaskHandle_t s_restart_delay_task;

typedef enum {
    CONFIG_JOB_IDLE,
    CONFIG_JOB_RUNNING,
    CONFIG_JOB_SAVED,
    CONFIG_JOB_FAILED,
} config_job_state_t;

static volatile config_job_state_t s_config_job_state;
static device_config_values_t s_config_job_candidate;
static char s_config_job_error[40];
static uint32_t s_config_job_generation;
static uint8_t s_config_job_slot = DEVICE_CONFIG_SLOT_NONE;

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool url_decode(const char *src, size_t src_len, char *dst, size_t dst_size)
{
    if (dst_size == 0) return false;
    size_t out = 0;
    for (size_t index = 0; index < src_len; index++) {
        unsigned char decoded;
        if (src[index] == '+') {
            decoded = ' ';
        } else if (src[index] == '%') {
            if (index + 2 >= src_len) return false;
            int high = hex_value(src[index + 1]);
            int low = hex_value(src[index + 2]);
            if (high < 0 || low < 0) return false;
            decoded = (unsigned char)((high << 4) | low);
            index += 2;
        } else {
            decoded = (unsigned char)src[index];
        }
        if (decoded == '\0' || out + 1 >= dst_size) return false;
        dst[out++] = (char)decoded;
    }
    dst[out] = '\0';
    return true;
}

static bool form_get_value(const char *body, const char *key, char *out, size_t out_size)
{
    const size_t key_len = strlen(key);
    const char *field = body;
    while (*field != '\0') {
        const char *end = strchr(field, '&');
        if (end == NULL) end = field + strlen(field);
        const char *equals = memchr(field, '=', (size_t)(end - field));
        if (equals != NULL && (size_t)(equals - field) == key_len &&
            strncmp(field, key, key_len) == 0) {
            return url_decode(equals + 1, (size_t)(end - equals - 1), out, out_size);
        }
        field = *end == '&' ? end + 1 : end;
    }
    return false;
}

static bool valid_student_field(const char *value, bool required)
{
    if (value == NULL || (required && value[0] == '\0')) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
        if (*p < 0x20 || *p == '|') return false;
    }
    return true;
}

static esp_err_t read_request_body(httpd_req_t *req, char **body,
                                   size_t maximum_size)
{
    if (req == NULL || body == NULL || req->content_len <= 0 ||
        (size_t)req->content_len > maximum_size) {
        return ESP_ERR_INVALID_ARG;
    }
    char *buffer = calloc((size_t)req->content_len + 1, 1);
    if (buffer == NULL) return ESP_ERR_NO_MEM;
    size_t total = 0;
    while (total < (size_t)req->content_len) {
        const int received = httpd_req_recv(req, buffer + total,
                                            req->content_len - total);
        if (received <= 0) {
            free(buffer);
            return ESP_FAIL;
        }
        total += (size_t)received;
    }
    buffer[total] = '\0';
    *body = buffer;
    return ESP_OK;
}

static void provisioning_event_handler(void *argument,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       void *event_data)
{
    (void)argument;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = event_data;
        ESP_LOGI(TAG, "Maintenance client connected (AID=%u)",
                 event != NULL ? (unsigned)event->aid : 0U);
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = event_data;
        ESP_LOGW(TAG, "Maintenance client disconnected (AID=%u reason=%u)",
                 event != NULL ? (unsigned)event->aid : 0U,
                 event != NULL ? (unsigned)event->reason : 0U);
    }
    if (!s_candidate_connecting || s_provisioning_events == NULL) return;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_provisioning_events, PROVISION_GOT_IP_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        ESP_LOGW(TAG, "Candidate Wi-Fi disconnected (reason=%u rssi=%d)",
                 event != NULL ? (unsigned)event->reason : 0U,
                 event != NULL ? (int)event->rssi : -127);
        xEventGroupSetBits(s_provisioning_events, PROVISION_DISCONNECTED_BIT);
    }
}

typedef enum {
    PROVISION_TEST_OK,
    PROVISION_TEST_WIFI_FAILED,
    PROVISION_TEST_TIME_FAILED,
    PROVISION_TEST_CLOUD_FAILED,
} provision_test_result_t;

static provision_test_result_t test_candidate_config(
    const device_config_values_t *candidate)
{
    wifi_config_t station = {0};
    strlcpy((char *)station.sta.ssid, candidate->wifi_ssid,
            sizeof(station.sta.ssid));
    strlcpy((char *)station.sta.password, candidate->wifi_password,
            sizeof(station.sta.password));
    station.sta.threshold.authmode = candidate->wifi_password[0] == '\0' ?
                                     WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    station.sta.pmf_cfg.capable = true;
    station.sta.pmf_cfg.required = false;

    s_candidate_connecting = false;
    (void)esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &station) != ESP_OK) {
        return PROVISION_TEST_WIFI_FAILED;
    }
    xEventGroupClearBits(s_provisioning_events,
                         PROVISION_GOT_IP_BIT | PROVISION_DISCONNECTED_BIT);
    s_candidate_connecting = true;
    if (esp_wifi_connect() != ESP_OK) {
        s_candidate_connecting = false;
        return PROVISION_TEST_WIFI_FAILED;
    }
    const EventBits_t bits = xEventGroupWaitBits(
        s_provisioning_events,
        PROVISION_GOT_IP_BIT | PROVISION_DISCONNECTED_BIT,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(PROVISION_WIFI_TIMEOUT_MS));
    if ((bits & PROVISION_GOT_IP_BIT) == 0) {
        s_candidate_connecting = false;
        (void)esp_wifi_disconnect();
        return PROVISION_TEST_WIFI_FAILED;
    }

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.google.com");
    bool sntp_started = esp_netif_sntp_init(&sntp_config) == ESP_OK;
    if (!sntp_started ||
        esp_netif_sntp_sync_wait(pdMS_TO_TICKS(PROVISION_NTP_TIMEOUT_MS)) != ESP_OK) {
        if (sntp_started) esp_netif_sntp_deinit();
        s_candidate_connecting = false;
        (void)esp_wifi_disconnect();
        return PROVISION_TEST_TIME_FAILED;
    }

    wifi_ap_record_t access_point = {0};
    int rssi = -127;
    if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) rssi = access_point.rssi;
    const esp_err_t cloud_error = http_upload_test_config(
        candidate->gas_url, candidate->device_secret, candidate->device_id, rssi);
    esp_netif_sntp_deinit();
    s_candidate_connecting = false;
    (void)esp_wifi_disconnect();
    return cloud_error == ESP_OK ? PROVISION_TEST_OK : PROVISION_TEST_CLOUD_FAILED;
}

static void config_job_task(void *argument)
{
    (void)argument;
    /* Let the HTTP task finish sending the accepted response before AP+STA
     * channel changes can interrupt the browser connection. */
    vTaskDelay(pdMS_TO_TICKS(1500));
    const provision_test_result_t test =
        test_candidate_config(&s_config_job_candidate);
    if (test != PROVISION_TEST_OK) {
        const char *error = test == PROVISION_TEST_WIFI_FAILED ?
            "wifi_connection_failed" : test == PROVISION_TEST_TIME_FAILED ?
            "ntp_sync_failed" : "cloud_authentication_failed";
        snprintf(s_config_job_error, sizeof(s_config_job_error), "%s", error);
        memset(&s_config_job_candidate, 0, sizeof(s_config_job_candidate));
        s_config_job_state = CONFIG_JOB_FAILED;
        vTaskDelete(NULL);
        return;
    }

    device_config_status_t status = {0};
    const esp_err_t save_error =
        device_config_save(&s_config_job_candidate, &status);
    memset(&s_config_job_candidate, 0, sizeof(s_config_job_candidate));
    if (save_error != ESP_OK) {
        snprintf(s_config_job_error, sizeof(s_config_job_error),
                 "config_save_failed");
        s_config_job_state = CONFIG_JOB_FAILED;
        vTaskDelete(NULL);
        return;
    }
    s_config_job_generation = status.generation;
    s_config_job_slot = status.active_slot;
    s_config_job_error[0] = '\0';
    s_config_job_state = CONFIG_JOB_SAVED;
    vTaskDelete(NULL);
}

static void delayed_restart_request_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(1500));
    s_restart_requested = true;
    s_done_requested = true;
    s_restart_delay_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t schedule_restart(void)
{
    if (s_restart_delay_task != NULL) return ESP_OK;
    if (xTaskCreate(delayed_restart_request_task, "portal_restart", 2048, NULL,
                    3, &s_restart_delay_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t send_json_string(httpd_req_t *req, const char *value)
{
    esp_err_t err = httpd_resp_send_chunk(req, "\"", 1);
    if (err != ESP_OK) return err;
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
        const char *escaped = NULL;
        if (*p == '\"') escaped = "\\\"";
        else if (*p == '\\') escaped = "\\\\";
        if (escaped != NULL) {
            err = httpd_resp_send_chunk(req, escaped, HTTPD_RESP_USE_STRLEN);
        } else {
            char one_char[2] = {(char)*p, '\0'};
            err = httpd_resp_send_chunk(req, one_char, 1);
        }
        if (err != ESP_OK) return err;
    }
    return httpd_resp_send_chunk(req, "\"", 1);
}

/* HTML/CSS/JS are embedded as source assets for easier maintenance. */
extern const char portal_enroll_html_start[] asm("_binary_portal_enroll_html_start");
extern const char portal_config_html_start[] asm("_binary_portal_config_html_start");

/* ─── GET / ──────────────────────────────────────────────────── */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    return httpd_resp_send(req, portal_enroll_html_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_config_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    return httpd_resp_send(req, portal_config_html_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_config_error(httpd_req_t *req, const char *error)
{
    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    char response[128];
    snprintf(response, sizeof(response), "{\"ok\":false,\"error\":\"%s\"}", error);
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_config_status(httpd_req_t *req)
{
    device_config_values_t values = {0};
    device_config_status_t status = {0};
    const bool configured = device_config_load(&values, &status) == ESP_OK;
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    char prefix[192];
    snprintf(prefix, sizeof(prefix),
             "{\"ok\":true,\"configured\":%s,\"rollback_available\":%s,"
             "\"generation\":%lu,\"active_slot\":\"%c\",\"ssid\":",
             configured ? "true" : "false",
             status.rollback_available ? "true" : "false",
             (unsigned long)status.generation,
             status.active_slot == 0 ? 'A' : status.active_slot == 1 ? 'B' : '-');
    esp_err_t error = httpd_resp_send_chunk(req, prefix, HTTPD_RESP_USE_STRLEN);
    if (error == ESP_OK) error = send_json_string(req, configured ? values.wifi_ssid : "");
    if (error == ESP_OK) {
        error = httpd_resp_send_chunk(req, ",\"device_id\":", HTTPD_RESP_USE_STRLEN);
    }
    if (error == ESP_OK) error = send_json_string(req, configured ? values.device_id : "");
    if (error == ESP_OK) error = httpd_resp_send_chunk(req, "}", 1);
    if (error == ESP_OK) error = httpd_resp_send_chunk(req, NULL, 0);
    return error;
}

static esp_err_t handler_wifi_scan(httpd_req_t *req)
{
    wifi_scan_config_t scan = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
        return send_config_error(req, "wifi_scan_failed");
    }
    uint16_t count = WIFI_SCAN_LIMIT;
    wifi_ap_record_t records[WIFI_SCAN_LIMIT] = {0};
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        return send_config_error(req, "wifi_scan_failed");
    }
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    esp_err_t error = httpd_resp_send_chunk(req, "{\"ok\":true,\"networks\":[",
                                            HTTPD_RESP_USE_STRLEN);
    for (uint16_t index = 0; error == ESP_OK && index < count; index++) {
        if (index > 0) error = httpd_resp_send_chunk(req, ",", 1);
        if (error != ESP_OK) break;
        error = httpd_resp_send_chunk(req, "{\"ssid\":", HTTPD_RESP_USE_STRLEN);
        if (error == ESP_OK) {
            error = send_json_string(req, (const char *)records[index].ssid);
        }
        if (error == ESP_OK) {
            char suffix[48];
            snprintf(suffix, sizeof(suffix), ",\"rssi\":%d}", records[index].rssi);
            error = httpd_resp_send_chunk(req, suffix, HTTPD_RESP_USE_STRLEN);
        }
    }
    if (error == ESP_OK) error = httpd_resp_send_chunk(req, "]}", 2);
    if (error == ESP_OK) error = httpd_resp_send_chunk(req, NULL, 0);
    return error;
}

static esp_err_t handler_config_save(httpd_req_t *req)
{
    char *body = NULL;
    if (read_request_body(req, &body, 3072) != ESP_OK) {
        return send_config_error(req, "invalid_body");
    }
    device_config_values_t candidate = {0};
    const bool parsed =
        form_get_value(body, "ssid", candidate.wifi_ssid,
                       sizeof(candidate.wifi_ssid)) &&
        form_get_value(body, "wifi_password", candidate.wifi_password,
                       sizeof(candidate.wifi_password)) &&
        form_get_value(body, "gas_url", candidate.gas_url,
                       sizeof(candidate.gas_url)) &&
        form_get_value(body, "device_id", candidate.device_id,
                       sizeof(candidate.device_id)) &&
        form_get_value(body, "device_secret", candidate.device_secret,
                       sizeof(candidate.device_secret));
    free(body);
    if (parsed) {
        device_config_values_t current = {0};
        if (device_config_load(&current, NULL) == ESP_OK) {
            /* Blank fields mean 'keep existing', never expose secrets in GET. */
            if (candidate.wifi_password[0] == '\0' &&
                strcmp(candidate.wifi_ssid, current.wifi_ssid) == 0) {
                memcpy(candidate.wifi_password, current.wifi_password,
                       sizeof(candidate.wifi_password));
            }
            if (candidate.gas_url[0] == '\0') {
                memcpy(candidate.gas_url, current.gas_url,
                       sizeof(candidate.gas_url));
            }
            if (candidate.device_id[0] == '\0') {
                memcpy(candidate.device_id, current.device_id,
                       sizeof(candidate.device_id));
            }
            if (candidate.device_secret[0] == '\0') {
                memcpy(candidate.device_secret, current.device_secret,
                       sizeof(candidate.device_secret));
            }
        }
        memset(&current, 0, sizeof(current));
    }
    if (!parsed || !device_config_values_valid(&candidate)) {
        memset(&candidate, 0, sizeof(candidate));
        return send_config_error(req, "invalid_configuration");
    }
    if (s_config_job_state == CONFIG_JOB_RUNNING) {
        memset(&candidate, 0, sizeof(candidate));
        return send_config_error(req, "config_test_busy");
    }
    s_config_job_candidate = candidate;
    memset(&candidate, 0, sizeof(candidate));
    s_config_job_error[0] = '\0';
    s_config_job_generation = 0;
    s_config_job_slot = DEVICE_CONFIG_SLOT_NONE;
    s_config_job_state = CONFIG_JOB_RUNNING;
    if (xTaskCreate(config_job_task, "config_test", CONFIG_JOB_TASK_STACK, NULL,
                    CONFIG_JOB_TASK_PRIORITY, NULL) != pdPASS) {
        memset(&s_config_job_candidate, 0, sizeof(s_config_job_candidate));
        s_config_job_state = CONFIG_JOB_IDLE;
        return send_config_error(req, "config_test_start_failed");
    }
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    return httpd_resp_send(req, "{\"ok\":true,\"state\":\"testing\"}",
                           HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_config_job(httpd_req_t *req)
{
    const config_job_state_t state = s_config_job_state;
    const char *state_name = state == CONFIG_JOB_RUNNING ? "running" :
                             state == CONFIG_JOB_SAVED ? "saved" :
                             state == CONFIG_JOB_FAILED ? "failed" : "idle";
    char response[160];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"state\":\"%s\",\"error\":\"%s\","
             "\"generation\":%lu,\"active_slot\":\"%c\"}",
             state_name, state == CONFIG_JOB_FAILED ? s_config_job_error : "",
             (unsigned long)s_config_job_generation,
             s_config_job_slot == 0 ? 'A' : s_config_job_slot == 1 ? 'B' : '-');
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_config_restart(httpd_req_t *req)
{
    if (s_config_job_state != CONFIG_JOB_SAVED) {
        return send_config_error(req, "no_saved_config_to_apply");
    }
    if (schedule_restart() != ESP_OK) {
        return send_config_error(req, "restart_schedule_failed");
    }
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    return httpd_resp_send(req, "{\"ok\":true,\"restarting\":true}",
                           HTTPD_RESP_USE_STRLEN);
}

static bool request_confirmation(httpd_req_t *req, const char *expected)
{
    char *body = NULL;
    if (read_request_body(req, &body, 128) != ESP_OK) return false;
    char confirmation[32] = {0};
    const bool accepted = form_get_value(body, "confirm", confirmation,
                                         sizeof(confirmation)) &&
                          strcmp(confirmation, expected) == 0;
    free(body);
    return accepted;
}

static esp_err_t handler_config_rollback(httpd_req_t *req)
{
    if (s_config_job_state == CONFIG_JOB_RUNNING) {
        return send_config_error(req, "config_test_busy");
    }
    if (!request_confirmation(req, "ROLLBACK")) {
        return send_config_error(req, "confirmation_required");
    }
    device_config_status_t status = {0};
    if (device_config_rollback(&status) != ESP_OK) {
        return send_config_error(req, "rollback_unavailable");
    }
    char response[72];
    snprintf(response, sizeof(response), "{\"ok\":true,\"generation\":%lu}",
             (unsigned long)status.generation);
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    if (schedule_restart() != ESP_OK) {
        return send_config_error(req, "restart_schedule_failed");
    }
    const esp_err_t response_error =
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return response_error;
}

static esp_err_t handler_config_reset(httpd_req_t *req)
{
    if (s_config_job_state == CONFIG_JOB_RUNNING) {
        return send_config_error(req, "config_test_busy");
    }
    if (!request_confirmation(req, "RESET CONFIG")) {
        return send_config_error(req, "confirmation_required");
    }
    if (device_config_erase() != ESP_OK) {
        return send_config_error(req, "config_reset_failed");
    }
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    if (schedule_restart() != ESP_OK) {
        return send_config_error(req, "restart_schedule_failed");
    }
    const esp_err_t response_error = httpd_resp_send(
        req, "{\"ok\":true,\"scope\":\"network_cloud_only\"}",
        HTTPD_RESP_USE_STRLEN);
    return response_error;
}

static esp_err_t handler_audio_status(httpd_req_t *req)
{
    audio_feedback_status_t status;
    audio_feedback_get_status(&status);
    char response[192];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"initialized\":%s,\"enabled\":%s,"
             "\"module_responding\":%s,\"volume\":%u,\"dropped_events\":%lu}",
             status.initialized ? "true" : "false",
             status.enabled ? "true" : "false",
             status.module_responding ? "true" : "false",
             status.volume, (unsigned long)status.dropped_events);
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_audio_config(httpd_req_t *req)
{
    char *body = NULL;
    if (read_request_body(req, &body, 128) != ESP_OK) {
        return send_config_error(req, "invalid_audio_body");
    }
    char enabled_text[4] = {0};
    char volume_text[4] = {0};
    const bool parsed =
        form_get_value(body, "enabled", enabled_text, sizeof(enabled_text)) &&
        form_get_value(body, "volume", volume_text, sizeof(volume_text));
    free(body);
    char *end = NULL;
    const long volume = parsed ? strtol(volume_text, &end, 10) : -1;
    if (!parsed ||
        (strcmp(enabled_text, "0") != 0 && strcmp(enabled_text, "1") != 0) ||
        end == volume_text || *end != '\0' ||
        volume < 0 || volume > (long)AUDIO_FEEDBACK_MAX_VOLUME) {
        return send_config_error(req, "invalid_audio_configuration");
    }
    const esp_err_t error =
        audio_feedback_set_config(strcmp(enabled_text, "1") == 0,
                                  (uint8_t)volume);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Cannot save/apply audio config: %s",
                 esp_err_to_name(error));
        return send_config_error(req, "audio_configuration_failed");
    }
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_audio_test(httpd_req_t *req)
{
    char *body = NULL;
    if (read_request_body(req, &body, 64) != ESP_OK) {
        return send_config_error(req, "invalid_audio_test");
    }
    char track_text[6] = {0};
    const bool parsed =
        form_get_value(body, "track", track_text, sizeof(track_text));
    free(body);
    char *end = NULL;
    const long track = parsed ? strtol(track_text, &end, 10) : -1;
    if (!parsed || end == track_text || *end != '\0' ||
        !((track >= 1 && track <= AUDIO_FEEDBACK_TRACK_COUNT) ||
          (track > AUDIO_FEEDBACK_STUDENT_TRACK_BASE &&
           track <= AUDIO_FEEDBACK_STUDENT_TRACK_LAST))) {
        return send_config_error(req, "invalid_audio_track");
    }
    if (audio_feedback_test_track((uint16_t)track) != ESP_OK) {
        return send_config_error(req, "audio_not_ready");
    }
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_students(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    esp_err_t err = httpd_resp_send_chunk(req, "{\"ok\":true,\"students\":[", HTTPD_RESP_USE_STRLEN);
    bool first = true;
    int count = 0;
    for (uint16_t id = 1; err == ESP_OK && id <= ENROLLMENT_CAPACITY; id++) {
        student_info_t student;
        if (student_db_get(id, &student) != ESP_OK) continue;
        if (!first) err = httpd_resp_send_chunk(req, ",", 1);
        if (err != ESP_OK) break;
        char prefix[48];
        snprintf(prefix, sizeof(prefix), "{\"id\":%u,\"name\":", (unsigned)id);
        err = httpd_resp_send_chunk(req, prefix, HTTPD_RESP_USE_STRLEN);
        if (err == ESP_OK) err = send_json_string(req, student.name);
        if (err == ESP_OK) err = httpd_resp_send_chunk(req, ",\"class\":", HTTPD_RESP_USE_STRLEN);
        if (err == ESP_OK) err = send_json_string(req, student.class_name);
        if (err == ESP_OK) err = httpd_resp_send_chunk(req, "}", 1);
        first = false;
        count++;
    }
    if (err == ESP_OK) {
        char suffix[128];
        const int face_count = face_engine_get_enrolled_count();
        snprintf(suffix, sizeof(suffix),
                 "],\"count\":%d,\"face_count\":%d,\"consistent\":%s,\"capacity\":%d}",
                 count, face_count, count == face_count ? "true" : "false",
                 ENROLLMENT_CAPACITY);
        err = httpd_resp_send_chunk(req, suffix, HTTPD_RESP_USE_STRLEN);
    }
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

/* ─── POST /enroll ───────────────────────────────────────────── */
static esp_err_t handler_enroll(httpd_req_t *req)
{
    char body[256] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_FAIL;
    }
    size_t total = 0;
    while (total < req->content_len) {
        int received = httpd_req_recv(req, body + total, req->content_len - total);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete body");
            return ESP_FAIL;
        }
        total += (size_t)received;
    }
    if (total == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    body[total] = '\0';

    /* Parse form-encoded: name=...&class=... */
    char name[48] = {0};
    char class_name[16] = {0};

    bool name_ok = form_get_value(body, "name", name, sizeof(name));
    bool class_ok = form_get_value(body, "class", class_name, sizeof(class_name));

    if (!name_ok || !class_ok || !valid_student_field(name, true) ||
        !valid_student_field(class_name, false)) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Dữ liệu tên/lớp không hợp lệ hoặc quá dài\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (face_engine_get_enrolled_count() >= STUDENT_MAX_COUNT) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Đã đạt giới hạn học sinh\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    /* Thực hiện enroll khuôn mặt */
    face_engine_result_t result = face_engine_enroll();
    char resp[128];
    if (result.state == FACE_ENGINE_ENROLLED) {
        uint16_t face_id = (uint16_t)result.id;
        esp_err_t db_err = student_db_set(face_id, name, class_name);
        if (db_err == ESP_OK) {
            esp_err_t marker_err = student_db_set_enrollment_complete(false);
            if (marker_err != ESP_OK) {
                ESP_LOGW(TAG, "Could not mark enrollment session active: %s",
                         esp_err_to_name(marker_err));
            }
            snprintf(resp, sizeof(resp), "{\"ok\":true,\"face_id\":%u}", face_id);
            audio_feedback_notify(AUDIO_EVENT_ENROLL_SUCCESS);
            ESP_LOGI(TAG, "Enrolled: id=%u name='%s' class='%s'", face_id, name, class_name);
        } else {
            ESP_LOGE(TAG, "Student metadata save failed: %s; rolling back face",
                     esp_err_to_name(db_err));
            if (face_engine_delete_last() != 0) {
                ESP_LOGE(TAG, "Face rollback failed for id=%u", face_id);
            }
            snprintf(resp, sizeof(resp),
                     "{\"ok\":false,\"error\":\"Không thể lưu thông tin học sinh\"}");
        }
    } else if (result.state == FACE_ENGINE_NO_FACE) {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"Không phát hiện khuôn mặt\"}");
    } else if (result.state == FACE_ENGINE_MULTIPLE_FACES) {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"Chỉ để một khuôn mặt trong khung hình\"}");
    } else {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"Lỗi nhận diện (code %d)\"}", result.state);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ─── POST /undo ─────────────────────────────────────────────── */
static esp_err_t handler_undo(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    int face_count = face_engine_get_enrolled_count();
    if (face_count <= 0) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Chưa có học sinh để hoàn tác\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    uint16_t removed_id = (uint16_t)face_count;
    if (face_engine_delete_last() != 0) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Không thể xóa khuôn mặt cuối\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    esp_err_t db_err = student_db_delete(removed_id);
    if (db_err != ESP_OK) {
        ESP_LOGE(TAG, "Deleted face id=%u but metadata delete failed: %s",
                 removed_id, esp_err_to_name(db_err));
        httpd_resp_send(req,
                        "{\"ok\":false,\"error\":\"Đã xóa khuôn mặt nhưng lỗi xóa thông tin\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    (void)student_db_set_enrollment_complete(false);
    char response[80];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"removed_id\":%u,\"count\":%d}",
             removed_id, face_engine_get_enrolled_count());
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "Enrollment undo: removed id=%u", removed_id);
    return ESP_OK;
}

/* ─── POST /done ─────────────────────────────────────────────── */
static esp_err_t handler_done(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    if (s_config_job_state == CONFIG_JOB_RUNNING) {
        httpd_resp_send(req,
                        "{\"ok\":false,\"error\":\"Đang kiểm tra cấu hình; hãy chờ hoàn tất\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    device_config_values_t runtime_config;
    if (device_config_load(&runtime_config, NULL) != ESP_OK) {
        httpd_resp_send(req,
                        "{\"ok\":false,\"error\":\"Hãy cấu hình và kiểm tra Wi-Fi/cloud trước\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    memset(&runtime_config, 0, sizeof(runtime_config));
    int face_count = face_engine_get_enrolled_count();
    if (face_count <= 0) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Chưa đăng ký học sinh nào\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    const int student_count = student_db_count();
    if (face_count != student_count) {
        httpd_resp_send(req,
                        "{\"ok\":false,\"error\":\"Số khuôn mặt và học sinh không khớp; hãy sửa hoặc reset danh tính\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    esp_err_t marker_err = student_db_set_enrollment_complete(true);
    if (marker_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not finish enrollment session: %s", esp_err_to_name(marker_err));
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Không thể lưu trạng thái hoàn tất\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    char response[64];
    snprintf(response, sizeof(response), "{\"ok\":true,\"count\":%d}", face_count);
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    s_done_requested = true;
    ESP_LOGI(TAG, "Teacher completed enrollment session with %d faces", face_count);
    return ESP_OK;
}

/* ─── POST /reset-identities ────────────────────────────────── */
static esp_err_t handler_reset_identities(httpd_req_t *req)
{
    char body[32] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid confirmation");
        return ESP_FAIL;
    }
    size_t total = 0;
    while (total < req->content_len) {
        const int received = httpd_req_recv(req, body + total,
                                            req->content_len - total);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Incomplete confirmation");
            return ESP_FAIL;
        }
        total += (size_t)received;
    }
    body[total] = '\0';
    char confirmation[8] = {0};
    if (!form_get_value(body, "confirm", confirmation, sizeof(confirmation)) ||
        strcmp(confirmation, "RESET") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Confirmation required");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    if (face_engine_clear_all() != 0 || student_db_clear_all() != ESP_OK) {
        ESP_LOGE(TAG, "Identity reset failed; database may require recovery");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Không thể xóa toàn bộ danh tính\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "All face embeddings and student metadata were reset");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ─── Public API ─────────────────────────────────────────────── */
esp_err_t enroll_server_start(const char *ap_ssid, const char *ap_pass)
{
    if (s_server != NULL || s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ap_ssid == NULL || ap_ssid[0] == '\0' || strlen(ap_ssid) > 32 ||
        ap_pass == NULL || strlen(ap_pass) < 8 || strlen(ap_pass) > 63) {
        ESP_LOGE(TAG, "Enrollment AP requires a valid SSID and WPA2 password");
        return ESP_ERR_INVALID_ARG;
    }
    s_done_requested = false;
    s_restart_requested = false;
    s_candidate_connecting = false;
    s_config_job_state = CONFIG_JOB_IDLE;
    memset(&s_config_job_candidate, 0, sizeof(s_config_job_candidate));
    s_config_job_error[0] = '\0';
    s_config_job_generation = 0;
    s_config_job_slot = DEVICE_CONFIG_SLOT_NONE;
    esp_err_t err;

    /* AP+STA keeps the local maintenance page reachable while a candidate
     * 2.4 GHz network and cloud credential are tested. */
    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_ap_netif == NULL || s_sta_netif == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    s_provisioning_events = xEventGroupCreate();
    if (s_provisioning_events == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) goto fail;
    s_wifi_initialized = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               provisioning_event_handler, NULL,
                                               &s_wifi_event_instance);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               provisioning_event_handler, NULL,
                                               &s_ip_event_instance);
    if (err != ESP_OK) goto fail;

    /* ── Cấu hình AP ── */
    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(ap_ssid);
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.channel = 6;
    strncpy((char *)ap_cfg.ap.password, ap_pass, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;
    /* The portal is a short-range interactive control surface. Disabling
     * modem sleep avoids delayed/missed beacons that make phones and Ubuntu
     * appear to lose the AP while the page is idle. */
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) goto fail;

    /* Khởi động HTTP server */
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 20;
    err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) goto fail;

    static const httpd_uri_t uri_root     = { .uri = "/",         .method = HTTP_GET,  .handler = handler_root     };
    static const httpd_uri_t uri_students = { .uri = "/students", .method = HTTP_GET,  .handler = handler_students };
    static const httpd_uri_t uri_enroll   = { .uri = "/enroll",   .method = HTTP_POST, .handler = handler_enroll   };
    static const httpd_uri_t uri_undo     = { .uri = "/undo",     .method = HTTP_POST, .handler = handler_undo     };
    static const httpd_uri_t uri_done     = { .uri = "/done",     .method = HTTP_POST, .handler = handler_done     };
    static const httpd_uri_t uri_reset = {
        .uri = "/reset-identities", .method = HTTP_POST,
        .handler = handler_reset_identities
    };
    static const httpd_uri_t uri_config = {
        .uri = "/config", .method = HTTP_GET, .handler = handler_config_page
    };
    static const httpd_uri_t uri_config_status = {
        .uri = "/config/status", .method = HTTP_GET,
        .handler = handler_config_status
    };
    static const httpd_uri_t uri_wifi_scan = {
        .uri = "/wifi-scan", .method = HTTP_GET, .handler = handler_wifi_scan
    };
    static const httpd_uri_t uri_config_save = {
        .uri = "/config/test-and-save", .method = HTTP_POST,
        .handler = handler_config_save
    };
    static const httpd_uri_t uri_config_job = {
        .uri = "/config/job", .method = HTTP_GET,
        .handler = handler_config_job
    };
    static const httpd_uri_t uri_config_restart = {
        .uri = "/config/restart", .method = HTTP_POST,
        .handler = handler_config_restart
    };
    static const httpd_uri_t uri_config_rollback = {
        .uri = "/config/rollback", .method = HTTP_POST,
        .handler = handler_config_rollback
    };
    static const httpd_uri_t uri_config_reset = {
        .uri = "/config/reset", .method = HTTP_POST,
        .handler = handler_config_reset
    };
    static const httpd_uri_t uri_audio_status = {
        .uri = "/audio/status", .method = HTTP_GET,
        .handler = handler_audio_status
    };
    static const httpd_uri_t uri_audio_config = {
        .uri = "/audio/config", .method = HTTP_POST,
        .handler = handler_audio_config
    };
    static const httpd_uri_t uri_audio_test = {
        .uri = "/audio/test", .method = HTTP_POST,
        .handler = handler_audio_test
    };
    err = httpd_register_uri_handler(s_server, &uri_root);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_students);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_enroll);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_undo);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_done);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_reset);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_config);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_config_status);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_wifi_scan);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_config_save);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_config_job);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_config_restart);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_config_rollback);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_config_reset);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_audio_status);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_audio_config);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_audio_test);
    if (err != ESP_OK) goto fail;

    ESP_LOGI(TAG, "Maintenance server UP — SSID: %s  IP: 192.168.4.1", ap_ssid);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "Enroll server start failed: %s", esp_err_to_name(err));
    enroll_server_stop();
    return err;
}

esp_err_t enroll_server_stop(void)
{
    esp_err_t first_error = ESP_OK;
    s_candidate_connecting = false;
    if (s_server) {
        esp_err_t err = httpd_stop(s_server);
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
        s_server = NULL;
    }
    if (s_wifi_started) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
        s_wifi_started = false;
    }
    if (s_wifi_initialized) {
        if (s_wifi_event_instance != NULL) {
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  s_wifi_event_instance);
            s_wifi_event_instance = NULL;
        }
        if (s_ip_event_instance != NULL) {
            esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  s_ip_event_instance);
            s_ip_event_instance = NULL;
        }
        esp_err_t err = esp_wifi_deinit();
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
        s_wifi_initialized = false;
    }
    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_sta_netif != NULL) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_provisioning_events != NULL) {
        vEventGroupDelete(s_provisioning_events);
        s_provisioning_events = NULL;
    }
    ESP_LOGI(TAG, "Enroll server stopped");
    return first_error;
}

bool enroll_server_done(void)
{
    return s_done_requested;
}

bool enroll_server_restart_requested(void)
{
    return s_restart_requested;
}
