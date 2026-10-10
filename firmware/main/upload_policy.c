#include "upload_policy.h"

uint32_t upload_policy_backoff_ms(uint32_t consecutive_failures,
                                  uint32_t random_value)
{
    if (consecutive_failures == 0) return 0;
    uint32_t delay = UPLOAD_BACKOFF_BASE_MS;
    for (uint32_t step = 1; step < consecutive_failures; step++) {
        if (delay >= UPLOAD_BACKOFF_MAX_MS / 2U) {
            delay = UPLOAD_BACKOFF_MAX_MS;
            break;
        }
        delay *= 2U;
    }
    if (delay > UPLOAD_BACKOFF_MAX_MS) delay = UPLOAD_BACKOFF_MAX_MS;
    const uint32_t quarter = delay / 4U;
    const uint32_t span = quarter * 2U;
    const uint32_t jittered =
        delay - quarter + (span == 0 ? 0 : random_value % (span + 1U));
    return jittered > UPLOAD_BACKOFF_MAX_MS ? UPLOAD_BACKOFF_MAX_MS : jittered;
}
