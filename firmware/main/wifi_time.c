#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_time.h"
#include "wifi_time_config.h"

#define WIFI_MAX_RETRIES   5
#define WIFI_RETRY_PAUSE_MS 30000
#define NTP_SERVER_PRIMARY   "time.google.com"
#define NTP_SERVER_SECONDARY "pool.ntp.org"
#define NTP_SERVER_FALLBACK  "time.cloudflare.com"

static const char *TAG = "wifi_time";
static bool s_started;
static bool s_sntp_started;
static bool s_wifi_initialized;
static bool s_wifi_running;
static bool s_stop_requested;
static volatile bool s_time_synced;
static volatile wifi_time_state_t s_state = WIFI_TIME_NOT_CONFIGURED;
static int s_retry_count;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static TaskHandle_t s_reconnect_task;

static void delayed_reconnect_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_PAUSE_MS));
    if (!s_stop_requested && s_wifi_running) {
        s_retry_count = 0;
        s_state = WIFI_TIME_CONNECTING;
        ESP_LOGI(TAG, "Retrying Wi-Fi after backoff");
        esp_wifi_connect();
    }
    s_reconnect_task = NULL;
    vTaskDelete(NULL);
}

static bool system_time_valid(void)
{
    time_t now = time(NULL);
    struct tm local_time = {0};
    localtime_r(&now, &local_time);
    return local_time.tm_year >= (2024 - 1900);
}

static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    if (!system_time_valid()) {
        ESP_LOGW(TAG, "SNTP callback received an invalid system time");
        return;
    }

    s_time_synced = true;
    s_state = WIFI_TIME_SYNCED;
    time_t now = time(NULL);
    struct tm local_time = {0};
    char timestamp[32] = {0};
    localtime_r(&now, &local_time);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);
    ESP_LOGI(TAG, "NTP time synchronized: %s ICT", timestamp);
}

static void start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST(NTP_SERVER_PRIMARY,
                                NTP_SERVER_SECONDARY,
                                NTP_SERVER_FALLBACK));
    config.sync_cb = time_sync_notification_cb;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        s_state = WIFI_TIME_FAILED;
        ESP_LOGE(TAG, "NTP initialization failed: %s", esp_err_to_name(err));
        return;
    }
    s_sntp_started = true;
    s_state = WIFI_TIME_SYNCING;
    ESP_LOGI(TAG, "NTP synchronization started (%s, %s, %s)",
             NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY, NTP_SERVER_FALLBACK);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (s_stop_requested) {
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count++ < WIFI_MAX_RETRIES) {
            ESP_LOGW(TAG, "Wi-Fi disconnected; retry %d/%d", s_retry_count, WIFI_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            s_state = WIFI_TIME_FAILED;
            ESP_LOGE(TAG, "Wi-Fi connection failed; retrying in %d seconds",
                     WIFI_RETRY_PAUSE_MS / 1000);
            if (s_reconnect_task == NULL) {
                xTaskCreate(delayed_reconnect_task, "wifi_retry", 2048, NULL, 3,
                            &s_reconnect_task);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        start_sntp();
        ESP_LOGI(TAG, "Wi-Fi connected; waiting for NTP time");
    }
}

esp_err_t wifi_time_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    const size_t ssid_length = strlen(CDP_WIFI_SSID);
    const size_t password_length = strlen(CDP_WIFI_PASSWORD);
    if (ssid_length == 0 || ssid_length > 32 || password_length > 63 ||
        (password_length > 0 && password_length < 8)) {
        s_state = WIFI_TIME_NOT_CONFIGURED;
        ESP_LOGW(TAG, "Wi-Fi credentials are invalid; configure wifi_time_config.h");
        return ESP_ERR_INVALID_ARG;
    }
    s_started = true;
    s_stop_requested = false;
    s_time_synced = false;
    s_retry_count = 0;
    esp_err_t result;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        result = ESP_ERR_NO_MEM;
        goto fail;
    }

    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  &wifi_event_handler, NULL,
                                                  &s_wifi_event_instance);
    if (result != ESP_OK) goto fail;
    result = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  &wifi_event_handler, NULL,
                                                  &s_ip_event_instance);
    if (result != ESP_OK) goto fail;

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init_config);
    if (result != ESP_OK) goto fail;
    s_wifi_initialized = true;
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, CDP_WIFI_SSID, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, CDP_WIFI_PASSWORD, sizeof(config.sta.password));
    config.sta.threshold.authmode = CDP_WIFI_PASSWORD[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) goto fail;
    result = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (result != ESP_OK) goto fail;
    result = esp_wifi_start();
    if (result != ESP_OK) goto fail;
    s_wifi_running = true;
    result = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Could not enable Wi-Fi power save: %s", esp_err_to_name(result));
    }
    s_state = WIFI_TIME_CONNECTING;
    ESP_LOGI(TAG, "Connecting to configured Wi-Fi network");
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(result));
    wifi_time_stop();
    s_state = WIFI_TIME_FAILED;
    return result;
}

bool wifi_time_is_synced(void)
{
    if (s_time_synced) {
        return true;
    }
    if (s_sntp_started && system_time_valid()) {
        s_time_synced = true;
        s_state = WIFI_TIME_SYNCED;
        ESP_LOGI(TAG, "NTP time detected as valid");
        return true;
    }
    return false;
}

wifi_time_state_t wifi_time_get_state(void)
{
    (void)wifi_time_is_synced();
    return s_state;
}

void wifi_time_stop(void)
{
    if (!s_started) {
        return;
    }
    s_stop_requested = true;
    if (s_reconnect_task != NULL) {
        vTaskDelete(s_reconnect_task);
        s_reconnect_task = NULL;
    }
    if (s_sntp_started) {
        esp_netif_sntp_deinit();
        s_sntp_started = false;
    }
    esp_err_t stop_result = ESP_OK;
    esp_err_t deinit_result = ESP_OK;
    if (s_wifi_running) {
        stop_result = esp_wifi_stop();
        s_wifi_running = false;
    }
    if (s_wifi_initialized) {
        deinit_result = esp_wifi_deinit();
        s_wifi_initialized = false;
    }
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
    if (s_sta_netif != NULL) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    ESP_LOGI(TAG, "Wi-Fi stopped after time sync: stop=%s deinit=%s",
             esp_err_to_name(stop_result), esp_err_to_name(deinit_result));
    s_started = false;
}
