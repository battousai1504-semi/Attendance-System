#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Kept for compatibility with the attendance module. The value now comes from
// the ESP32 system clock, synchronized by Wi-Fi NTP in wifi_time.c.
void manual_clock_init(void);
time_t manual_clock_now(void);
void manual_clock_format(char *buffer, size_t buffer_size);
bool manual_clock_is_valid(void);
