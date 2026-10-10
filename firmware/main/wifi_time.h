#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
  WIFI_TIME_NOT_CONFIGURED,
  WIFI_TIME_CONNECTING,
  WIFI_TIME_SYNCING,
  WIFI_TIME_SYNCED,
  WIFI_TIME_FAILED,
} wifi_time_state_t;

esp_err_t wifi_time_start(const char *ssid, const char *password);
bool wifi_time_is_synced(void);
wifi_time_state_t wifi_time_get_state(void);
void wifi_time_stop(void);
