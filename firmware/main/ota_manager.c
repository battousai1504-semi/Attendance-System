#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"

#include "app_version.h"
#include "http_upload.h"
#include "manual_clock.h"
#include "ota_manager.h"
#include "ota_manifest.h"

#define OTA_TASK_STACK             10240
#define OTA_TASK_PRIORITY          3
#define OTA_TASK_CORE              1
#define OTA_FIRST_CHECK_DELAY_MS   60000
#define OTA_CHECK_INTERVAL_MS      (6U * 60U * 60U * 1000U)
#define OTA_RETRY_INTERVAL_MS      (15U * 60U * 1000U)
#define OTA_HTTP_TIMEOUT_MS        30000
#define OTA_HASH_BUFFER_SIZE       4096
#define OTA_RESPONSE_SIZE          1024

static const char *TAG = "ota_manager";
static volatile bool s_busy;
static bool s_started;

static bool running_image_pending(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    return running != NULL &&
           esp_ota_get_state_partition(running, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

void ota_manager_confirm_running_image(void)
{
    if (!running_image_pending()) return;
    const esp_err_t error = esp_ota_mark_app_valid_cancel_rollback();
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "OTA health gate passed; running image marked VALID");
    } else {
        ESP_LOGE(TAG, "Could not confirm OTA image: %s", esp_err_to_name(error));
    }
}

void ota_manager_reject_pending_image(const char *reason)
{
    if (!running_image_pending()) return;
    ESP_LOGE(TAG, "OTA health gate failed (%s); rolling back",
             reason == NULL ? "unknown" : reason);
    const esp_err_t error = esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(TAG, "Automatic rollback failed: %s", esp_err_to_name(error));
}

static esp_err_t calculate_raw_sha256(const esp_partition_t *partition,
                                      size_t length, uint8_t digest[32])
{
    uint8_t *buffer = heap_caps_malloc(OTA_HASH_BUFFER_SIZE,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buffer == NULL) return ESP_ERR_NO_MEM;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    psa_status_t status = psa_crypto_init();
    if (status == PSA_SUCCESS) {
        status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
    }
    esp_err_t result = status == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
    for (size_t offset = 0; result == ESP_OK && offset < length;) {
        size_t chunk = length - offset;
        if (chunk > OTA_HASH_BUFFER_SIZE) chunk = OTA_HASH_BUFFER_SIZE;
        result = esp_partition_read(partition, offset, buffer, chunk);
        if (result == ESP_OK &&
            psa_hash_update(&operation, buffer, chunk) != PSA_SUCCESS) {
            result = ESP_FAIL;
        }
        offset += chunk;
    }
    size_t digest_length = 0;
    if (result == ESP_OK &&
        (psa_hash_finish(&operation, digest, 32, &digest_length) != PSA_SUCCESS ||
         digest_length != 32)) {
        result = ESP_FAIL;
    }
    if (result != ESP_OK) psa_hash_abort(&operation);
    free(buffer);
    return result;
}

static esp_err_t install_manifest(const ota_manifest_t *manifest)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL || target->size < manifest->size) {
        ESP_LOGE(TAG, "No OTA slot large enough for %lu bytes",
                 (unsigned long)manifest->size);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "Downloading OTA %s (%lu bytes) to %s",
             manifest->version, (unsigned long)manifest->size, target->label);

    esp_http_client_config_t http_config = {
        .url = manifest->url,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        .max_redirection_count = 5,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .bulk_flash_erase = false,
    };
    esp_https_ota_handle_t handle = NULL;
    esp_err_t error = esp_https_ota_begin(&ota_config, &handle);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "OTA connection failed: %s", esp_err_to_name(error));
        return error;
    }

    esp_app_desc_t image = {0};
    error = esp_https_ota_get_img_desc(handle, &image);
    if (error != ESP_OK ||
        strncmp(image.version, manifest->version, sizeof(image.version)) != 0) {
        ESP_LOGE(TAG, "OTA image version does not match manifest (image='%.32s', manifest='%s')",
                 image.version, manifest->version);
        esp_https_ota_abort(handle);
        return error == ESP_OK ? ESP_ERR_INVALID_VERSION : error;
    }
    const int advertised_size = esp_https_ota_get_image_size(handle);
    if (advertised_size >= 0 && (uint32_t)advertised_size != manifest->size) {
        ESP_LOGE(TAG, "OTA Content-Length mismatch: server=%d manifest=%lu",
                 advertised_size, (unsigned long)manifest->size);
        esp_https_ota_abort(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    int last_percent = -10;
    do {
        error = esp_https_ota_perform(handle);
        const int received = esp_https_ota_get_image_len_read(handle);
        if (received >= 0 && manifest->size > 0) {
            const int percent = (int)(((uint64_t)received * 100U) / manifest->size);
            if (percent >= last_percent + 10) {
                last_percent = percent;
                ESP_LOGI(TAG, "OTA download %d%%", percent > 100 ? 100 : percent);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (error == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    const int received = esp_https_ota_get_image_len_read(handle);
    if (error != ESP_OK || !esp_https_ota_is_complete_data_received(handle) ||
        received < 0 || (uint32_t)received != manifest->size) {
        ESP_LOGE(TAG, "OTA download incomplete: err=%s received=%d expected=%lu",
                 esp_err_to_name(error), received, (unsigned long)manifest->size);
        esp_https_ota_abort(handle);
        return error == ESP_OK ? ESP_ERR_INVALID_SIZE : error;
    }

    uint8_t actual_sha256[32];
    error = calculate_raw_sha256(target, manifest->size, actual_sha256);
    if (error != ESP_OK ||
        memcmp(actual_sha256, manifest->sha256, sizeof(actual_sha256)) != 0) {
        ESP_LOGE(TAG, "OTA SHA-256 verification failed; boot partition unchanged");
        esp_https_ota_abort(handle);
        return error == ESP_OK ? ESP_ERR_INVALID_CRC : error;
    }

    error = esp_https_ota_finish(handle);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "OTA image validation/finalization failed: %s",
                 esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "OTA %s verified and selected; restarting", manifest->version);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static void ota_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(OTA_FIRST_CHECK_DELAY_MS));
    while (true) {
        if (!manual_clock_is_valid()) {
            vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_INTERVAL_MS));
            continue;
        }
        char response[OTA_RESPONSE_SIZE];
        esp_err_t error = http_upload_fetch_ota_manifest(response, sizeof(response));
        ota_manifest_t manifest;
        if (error != ESP_OK ||
            !ota_manifest_parse_json(response, CDP_FIRMWARE_VERSION, &manifest)) {
            ESP_LOGW(TAG, "OTA manifest unavailable or invalid");
            vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_INTERVAL_MS));
            continue;
        }
        if (!manifest.available) {
            ESP_LOGI(TAG, "No OTA update for %s", CDP_FIRMWARE_VERSION);
            vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS));
            continue;
        }

        s_busy = true;
        error = http_upload_suspend_for_ota();
        if (error == ESP_OK) {
            error = install_manifest(&manifest);
            http_upload_resume_after_ota();
        }
        s_busy = false;
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "OTA %s rejected: %s", manifest.version,
                     esp_err_to_name(error));
        }
        vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_INTERVAL_MS));
    }
}

esp_err_t ota_manager_start(void)
{
    if (s_started) return ESP_OK;
    if (esp_ota_get_app_partition_count() < 2) return ESP_ERR_NOT_SUPPORTED;
    s_started = true;
    if (xTaskCreatePinnedToCore(ota_task, "ota_manager", OTA_TASK_STACK, NULL,
                                OTA_TASK_PRIORITY, NULL, OTA_TASK_CORE) != pdPASS) {
        s_started = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "P2.5 OTA ready: current=%s first_check=%ds interval=%dh",
             CDP_FIRMWARE_VERSION, OTA_FIRST_CHECK_DELAY_MS / 1000,
             OTA_CHECK_INTERVAL_MS / (60 * 60 * 1000));
    return ESP_OK;
}

bool ota_manager_busy(void)
{
    return s_busy;
}
