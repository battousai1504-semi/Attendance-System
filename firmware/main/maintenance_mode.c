#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "board_pins.h"
#include "maintenance_mode.h"

#define MAINTENANCE_NAMESPACE "maintenance"
#define ENROLLMENT_REQUEST_KEY "enroll_once"
#define BUTTON_POLL_MS 50
#define BUTTON_TASK_STACK 2048
#define BUTTON_TASK_PRIORITY 3

static const char *TAG = "maintenance";
static bool s_started;

static esp_err_t store_enrollment_request(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(MAINTENANCE_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;

    error = nvs_set_u8(handle, ENROLLMENT_REQUEST_KEY, 1);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

bool maintenance_mode_enrollment_requested(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(MAINTENANCE_NAMESPACE, NVS_READONLY, &handle);
    if (error != ESP_OK) {
        if (error == ESP_ERR_NVS_NOT_FOUND) return false;
        ESP_LOGE(TAG, "Cannot open maintenance state: %s", esp_err_to_name(error));
        return false;
    }

    uint8_t requested = 0;
    error = nvs_get_u8(handle, ENROLLMENT_REQUEST_KEY, &requested);
    nvs_close(handle);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Cannot read maintenance state: %s", esp_err_to_name(error));
    }
    if (error == ESP_OK && requested == 1) {
        ESP_LOGW(TAG, "Latched maintenance portal request active");
        return true;
    }
    return false;
}

esp_err_t maintenance_mode_clear_enrollment_request(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(MAINTENANCE_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;

    error = nvs_erase_key(handle, ENROLLMENT_REQUEST_KEY);
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Maintenance portal request cleared");
    }
    return error;
}

static void button_task(void *argument)
{
    (void)argument;
    int held_ms = 0;
    bool hold_logged = false;

    while (true) {
        if (gpio_get_level(ENROLL_BUTTON_GPIO) == ENROLL_BUTTON_ACTIVE_LEVEL) {
            if (!hold_logged) {
                ESP_LOGI(TAG, "BOOT pressed; keep holding for %d ms to enter maintenance",
                         ENROLL_BUTTON_HOLD_MS);
                hold_logged = true;
            }
            held_ms += BUTTON_POLL_MS;
            if (held_ms >= ENROLL_BUTTON_HOLD_MS) {
                const esp_err_t error = store_enrollment_request();
                if (error == ESP_OK) {
                    ESP_LOGW(TAG, "Maintenance requested; restarting into portal mode");
                    vTaskDelay(pdMS_TO_TICKS(300));
                    esp_restart();
                }
                ESP_LOGE(TAG, "Cannot persist maintenance request: %s",
                         esp_err_to_name(error));
                held_ms = 0;
                hold_logged = false;
            }
        } else {
            held_ms = 0;
            hold_logged = false;
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

esp_err_t maintenance_mode_start(void)
{
    if (s_started) return ESP_OK;

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << ENROLL_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t error = gpio_config(&config);
    if (error != ESP_OK) return error;

    if (xTaskCreate(button_task, "maintenance_button", BUTTON_TASK_STACK, NULL,
                    BUTTON_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "Runtime BOOT-button maintenance monitor ready");
    return ESP_OK;
}
