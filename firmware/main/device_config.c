#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "device_config.h"

#define CONFIG_NAMESPACE "device_cfg"
#define ACTIVE_SLOT_KEY   "active"
#define SLOT_A_KEY        "slot_a"
#define SLOT_B_KEY        "slot_b"

static const char *TAG = "device_config";

typedef struct {
    bool valid[2];
    device_config_record_t record[2];
    uint8_t requested_active;
    uint8_t selected;
} config_snapshot_t;

static const char *slot_key(uint8_t slot)
{
    return slot == 0 ? SLOT_A_KEY : SLOT_B_KEY;
}

static void read_slot(nvs_handle_t handle, uint8_t slot, config_snapshot_t *snapshot)
{
    size_t size = sizeof(snapshot->record[slot]);
    const esp_err_t error = nvs_get_blob(handle, slot_key(slot),
                                         &snapshot->record[slot], &size);
    snapshot->valid[slot] = error == ESP_OK &&
                            size == sizeof(snapshot->record[slot]) &&
                            device_config_record_valid(&snapshot->record[slot]);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Cannot read config slot %u: %s", slot, esp_err_to_name(error));
    } else if (error == ESP_OK && !snapshot->valid[slot]) {
        ESP_LOGW(TAG, "Ignoring invalid config slot %u", slot);
    }
}

static esp_err_t snapshot_load(nvs_handle_t handle, config_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->requested_active = DEVICE_CONFIG_SLOT_NONE;
    esp_err_t active_error = nvs_get_u8(handle, ACTIVE_SLOT_KEY,
                                        &snapshot->requested_active);
    if (active_error != ESP_OK && active_error != ESP_ERR_NVS_NOT_FOUND) {
        return active_error;
    }
    read_slot(handle, 0, snapshot);
    read_slot(handle, 1, snapshot);
    snapshot->selected = device_config_choose_slot(
        snapshot->requested_active,
        snapshot->valid[0], snapshot->record[0].generation,
        snapshot->valid[1], snapshot->record[1].generation);
    return ESP_OK;
}

static void fill_status(const config_snapshot_t *snapshot, device_config_status_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    status->active_slot = snapshot->selected;
    status->configured = snapshot->selected != DEVICE_CONFIG_SLOT_NONE;
    if (!status->configured) return;
    status->generation = snapshot->record[snapshot->selected].generation;
    status->rollback_available = snapshot->valid[1U - snapshot->selected];
}

esp_err_t device_config_load(device_config_values_t *values,
                             device_config_status_t *status)
{
    if (values == NULL) return ESP_ERR_INVALID_ARG;
    if (status != NULL) {
        memset(status, 0, sizeof(*status));
        status->active_slot = DEVICE_CONFIG_SLOT_NONE;
    }
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (error != ESP_OK) return error;
    config_snapshot_t snapshot;
    error = snapshot_load(handle, &snapshot);
    nvs_close(handle);
    if (error != ESP_OK) return error;
    fill_status(&snapshot, status);
    if (snapshot.selected == DEVICE_CONFIG_SLOT_NONE) return ESP_ERR_NVS_NOT_FOUND;
    *values = snapshot.record[snapshot.selected].values;
    ESP_LOGI(TAG, "Loaded runtime config schema=%u generation=%lu slot=%c",
             DEVICE_CONFIG_SCHEMA_VERSION,
             (unsigned long)snapshot.record[snapshot.selected].generation,
             snapshot.selected == 0 ? 'A' : 'B');
    return ESP_OK;
}

esp_err_t device_config_save(const device_config_values_t *values,
                             device_config_status_t *status)
{
    if (!device_config_values_valid(values)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;

    config_snapshot_t before;
    error = snapshot_load(handle, &before);
    if (error != ESP_OK) goto done;
    const uint8_t target = before.selected == 0 ? 1 : 0;
    if (before.selected != DEVICE_CONFIG_SLOT_NONE &&
        before.record[before.selected].generation == UINT32_MAX) {
        error = ESP_ERR_INVALID_STATE;
        goto done;
    }
    const uint32_t generation = before.selected == DEVICE_CONFIG_SLOT_NONE ? 1 :
        before.record[before.selected].generation + 1;
    device_config_record_t candidate;
    if (!device_config_record_prepare(values, generation, &candidate)) {
        error = ESP_ERR_INVALID_ARG;
        goto done;
    }

    /* Two commits are deliberate: a power loss before the active pointer is
     * switched leaves the previous slot authoritative. */
    error = nvs_set_blob(handle, slot_key(target), &candidate, sizeof(candidate));
    if (error == ESP_OK) error = nvs_commit(handle);
    if (error != ESP_OK) goto done;

    device_config_record_t verify = {0};
    size_t verify_size = sizeof(verify);
    error = nvs_get_blob(handle, slot_key(target), &verify, &verify_size);
    if (error != ESP_OK || verify_size != sizeof(verify) ||
        !device_config_record_valid(&verify)) {
        error = ESP_FAIL;
        goto done;
    }
    error = nvs_set_u8(handle, ACTIVE_SLOT_KEY, target);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (error == ESP_OK) {
        config_snapshot_t after;
        error = snapshot_load(handle, &after);
        if (error == ESP_OK) fill_status(&after, status);
        ESP_LOGI(TAG, "Activated runtime config schema=%u generation=%lu slot=%c",
                 DEVICE_CONFIG_SCHEMA_VERSION, (unsigned long)generation,
                 target == 0 ? 'A' : 'B');
    }

done:
    nvs_close(handle);
    return error;
}

esp_err_t device_config_rollback(device_config_status_t *status)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    config_snapshot_t snapshot;
    error = snapshot_load(handle, &snapshot);
    if (error != ESP_OK) goto done;
    if (snapshot.selected == DEVICE_CONFIG_SLOT_NONE) {
        error = ESP_ERR_NVS_NOT_FOUND;
        goto done;
    }
    const uint8_t previous = 1U - snapshot.selected;
    if (!snapshot.valid[previous]) {
        error = ESP_ERR_NOT_FOUND;
        goto done;
    }
    error = nvs_set_u8(handle, ACTIVE_SLOT_KEY, previous);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (error == ESP_OK) {
        snapshot.requested_active = previous;
        snapshot.selected = previous;
        fill_status(&snapshot, status);
        ESP_LOGW(TAG, "Rolled back runtime config to generation=%lu slot=%c",
                 (unsigned long)snapshot.record[previous].generation,
                 previous == 0 ? 'A' : 'B');
    }

done:
    nvs_close(handle);
    return error;
}

esp_err_t device_config_erase(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_erase_all(handle);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    if (error == ESP_OK) {
        ESP_LOGW(TAG, "Runtime network/cloud configuration erased; identities preserved");
    }
    return error;
}
