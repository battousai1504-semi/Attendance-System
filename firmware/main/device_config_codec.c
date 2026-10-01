#include <stddef.h>
#include <string.h>

#include "device_config.h"

static bool terminated_within(const char *value, size_t capacity, size_t *length)
{
    const char *end = memchr(value, '\0', capacity);
    if (end == NULL) return false;
    if (length != NULL) *length = (size_t)(end - value);
    return true;
}

static bool printable_text(const char *value, size_t length, bool allow_space)
{
    for (size_t index = 0; index < length; index++) {
        const unsigned char c = (unsigned char)value[index];
        if (c < 0x20 || c == 0x7f || (!allow_space && c == ' ')) return false;
    }
    return true;
}

static bool valid_device_id(const char *value, size_t length)
{
    if (length == 0) return false;
    for (size_t index = 0; index < length; index++) {
        const unsigned char c = (unsigned char)value[index];
        const bool allowed = (c >= 'a' && c <= 'z') ||
                             (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                             c == ':' || c == '-';
        if (!allowed) return false;
    }
    return true;
}

uint32_t device_config_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xffffffffU;
    for (size_t index = 0; index < length; index++) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; bit++) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

bool device_config_values_valid(const device_config_values_t *values)
{
    if (values == NULL) return false;
    size_t ssid_length = 0;
    size_t password_length = 0;
    size_t url_length = 0;
    size_t device_id_length = 0;
    size_t secret_length = 0;
    if (!terminated_within(values->wifi_ssid, sizeof(values->wifi_ssid), &ssid_length) ||
        !terminated_within(values->wifi_password, sizeof(values->wifi_password),
                           &password_length) ||
        !terminated_within(values->gas_url, sizeof(values->gas_url), &url_length) ||
        !terminated_within(values->device_id, sizeof(values->device_id),
                           &device_id_length) ||
        !terminated_within(values->device_secret, sizeof(values->device_secret),
                           &secret_length)) {
        return false;
    }
    if (ssid_length == 0 || ssid_length > DEVICE_CONFIG_WIFI_SSID_MAX ||
        password_length > DEVICE_CONFIG_WIFI_PASSWORD_MAX ||
        (password_length > 0 && password_length < 8) ||
        secret_length < 32 || secret_length > DEVICE_CONFIG_SECRET_MAX) {
        return false;
    }
    static const char prefix[] = "https://script.google.com/macros/s/";
    if (url_length <= sizeof(prefix) - 1 ||
        strncmp(values->gas_url, prefix, sizeof(prefix) - 1) != 0 ||
        url_length < 5 || strcmp(values->gas_url + url_length - 5, "/exec") != 0 ||
        strstr(values->gas_url, "YOUR_DEPLOYMENT_ID") != NULL ||
        strstr(values->device_secret, "replace-with") != NULL) {
        return false;
    }
    return printable_text(values->wifi_ssid, ssid_length, true) &&
           printable_text(values->wifi_password, password_length, true) &&
           printable_text(values->gas_url, url_length, false) &&
           valid_device_id(values->device_id, device_id_length) &&
           printable_text(values->device_secret, secret_length, true);
}

bool device_config_record_prepare(const device_config_values_t *values,
                                  uint32_t generation,
                                  device_config_record_t *record)
{
    if (record == NULL || generation == 0 || !device_config_values_valid(values)) {
        return false;
    }
    memset(record, 0, sizeof(*record));
    record->magic = DEVICE_CONFIG_MAGIC;
    record->schema_version = DEVICE_CONFIG_SCHEMA_VERSION;
    record->payload_size = sizeof(record->values);
    record->generation = generation;
    record->values = *values;
    record->checksum = device_config_crc32(record, offsetof(device_config_record_t, checksum));
    return true;
}

bool device_config_record_valid(const device_config_record_t *record)
{
    if (record == NULL || record->magic != DEVICE_CONFIG_MAGIC ||
        record->schema_version != DEVICE_CONFIG_SCHEMA_VERSION ||
        record->payload_size != sizeof(record->values) || record->generation == 0 ||
        !device_config_values_valid(&record->values)) {
        return false;
    }
    return record->checksum ==
           device_config_crc32(record, offsetof(device_config_record_t, checksum));
}

uint8_t device_config_choose_slot(uint8_t requested_active_slot,
                                  bool slot_a_valid,
                                  uint32_t slot_a_generation,
                                  bool slot_b_valid,
                                  uint32_t slot_b_generation)
{
    if (requested_active_slot == 0 && slot_a_valid) return 0;
    if (requested_active_slot == 1 && slot_b_valid) return 1;
    if (slot_a_valid && slot_b_valid) {
        return slot_b_generation > slot_a_generation ? 1 : 0;
    }
    if (slot_a_valid) return 0;
    if (slot_b_valid) return 1;
    return DEVICE_CONFIG_SLOT_NONE;
}
