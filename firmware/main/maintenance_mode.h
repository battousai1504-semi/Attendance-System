#pragma once

#include <stdbool.h>

#include "esp_err.h"

/*
 * Returns a latched maintenance-portal request stored in NVS. The request is
 * deliberately retained across unexpected resets so a watchdog, brownout or
 * brief power loss cannot silently close the local portal. nvs_flash_init()
 * must have completed before this function is called.
 */
bool maintenance_mode_enrollment_requested(void);

/* Clears the latched request after the user explicitly leaves maintenance. */
esp_err_t maintenance_mode_clear_enrollment_request(void);

/*
 * Starts a background BOOT-button monitor. Holding BOOT while the application
 * is already running stores a latched request and restarts the device. This
 * deliberately avoids sampling GPIO0 during reset, when it is a ROM strapping
 * pin that selects download mode.
 */
esp_err_t maintenance_mode_start(void);
