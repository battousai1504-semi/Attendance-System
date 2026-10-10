#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* Rollback health gate used by app_main around critical hardware startup. */
void ota_manager_confirm_running_image(void);
void ota_manager_reject_pending_image(const char *reason);

/* Starts the low-priority authenticated manifest checker on core 1. */
esp_err_t ota_manager_start(void);
bool ota_manager_busy(void);
