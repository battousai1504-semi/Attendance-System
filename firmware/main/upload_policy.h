#pragma once

#include <stdint.h>

#define UPLOAD_BACKOFF_BASE_MS 5000U
#define UPLOAD_BACKOFF_MAX_MS  300000U

/* Exponential delay with deterministic +/-25% jitter supplied by random_value. */
uint32_t upload_policy_backoff_ms(uint32_t consecutive_failures,
                                  uint32_t random_value);
