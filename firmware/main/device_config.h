#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DEVICE_CONFIG_MAGIC          0x43445032U
#define DEVICE_CONFIG_SCHEMA_VERSION 1U
#define DEVICE_CONFIG_SLOT_NONE      0xffU

#define DEVICE_CONFIG_WIFI_SSID_MAX     32
#define DEVICE_CONFIG_WIFI_PASSWORD_MAX 63
#define DEVICE_CONFIG_GAS_URL_MAX       383
#define DEVICE_CONFIG_DEVICE_ID_MAX     31
#define DEVICE_CONFIG_SECRET_MAX        127

/* The setup AP exists only after a physical BOOT-button maintenance request
 * or when no valid runtime configuration exists. This password protects the
 * local AP from accidental association; device/cloud secrets are never served
 * back by the portal. */
#define DEVICE_SETUP_AP_SSID     "RobotSetup"
#define DEVICE_SETUP_AP_PASSWORD "RobotSetup-2026"

typedef struct {
    char wifi_ssid[DEVICE_CONFIG_WIFI_SSID_MAX + 1];
    char wifi_password[DEVICE_CONFIG_WIFI_PASSWORD_MAX + 1];
    char gas_url[DEVICE_CONFIG_GAS_URL_MAX + 1];
    char device_id[DEVICE_CONFIG_DEVICE_ID_MAX + 1];
    char device_secret[DEVICE_CONFIG_SECRET_MAX + 1];
} device_config_values_t;

typedef struct {
    uint32_t magic;
    uint16_t schema_version;
    uint16_t payload_size;
    uint32_t generation;
    device_config_values_t values;
    uint32_t checksum;
} device_config_record_t;

typedef struct {
    bool configured;
    bool rollback_available;
    uint8_t active_slot;
    uint32_t generation;
} device_config_status_t;

/* Pure codec/validation functions, also exercised by host tests. */
uint32_t device_config_crc32(const void *data, size_t length);
bool device_config_values_valid(const device_config_values_t *values);
bool device_config_record_prepare(const device_config_values_t *values,
                                  uint32_t generation,
                                  device_config_record_t *record);
bool device_config_record_valid(const device_config_record_t *record);
uint8_t device_config_choose_slot(uint8_t requested_active_slot,
                                  bool slot_a_valid,
                                  uint32_t slot_a_generation,
                                  bool slot_b_valid,
                                  uint32_t slot_b_generation);

/* NVS-backed A/B store. nvs_flash_init() must have completed first. */
esp_err_t device_config_load(device_config_values_t *values,
                             device_config_status_t *status);
esp_err_t device_config_save(const device_config_values_t *values,
                             device_config_status_t *status);
esp_err_t device_config_rollback(device_config_status_t *status);
esp_err_t device_config_erase(void);
