#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_FEEDBACK_MIN_VOLUME      0U
#define AUDIO_FEEDBACK_MAX_VOLUME     30U
#define AUDIO_FEEDBACK_DEFAULT_VOLUME 18U
#define AUDIO_FEEDBACK_TRACK_COUNT     6U
#define AUDIO_FEEDBACK_STUDENT_TRACK_BASE 1000U
#define AUDIO_FEEDBACK_STUDENT_MAX_ID     50U
#define AUDIO_FEEDBACK_STUDENT_TRACK_LAST \
    (AUDIO_FEEDBACK_STUDENT_TRACK_BASE + AUDIO_FEEDBACK_STUDENT_MAX_ID)

typedef enum {
    AUDIO_EVENT_SYSTEM_READY = 0,
    AUDIO_EVENT_ATTENDANCE_SUCCESS,
    AUDIO_EVENT_ATTENDANCE_DUPLICATE,
    AUDIO_EVENT_TIME_UNAVAILABLE,
    AUDIO_EVENT_ENROLL_SUCCESS,
    AUDIO_EVENT_SYSTEM_ERROR,
    AUDIO_EVENT_COUNT,
} audio_feedback_event_t;

typedef struct {
    bool initialized;
    bool enabled;
    bool module_responding;
    uint8_t volume;
    uint32_t dropped_events;
} audio_feedback_status_t;

/* Optional peripheral: failure is reported to the caller but must never make
 * attendance, camera, provisioning or OTA fatal. NVS must be initialized. */
esp_err_t audio_feedback_init(void);

/* Non-blocking notification. Returns false when audio is disabled, not ready,
 * throttled or its small queue is full. */
bool audio_feedback_notify(audio_feedback_event_t event);

/* One complete spoken sentence per enrolled student, stored on DFPlayer SD as
 * /mp3/1001.mp3 ... /mp3/1050.mp3 for Student IDs 1 ... 50. Non-blocking. */
bool audio_feedback_notify_attendance(uint16_t student_id);

/* Persists settings in a dedicated CRC-protected NVS record and applies them
 * asynchronously. Volume is the DFPlayer native 0..30 range. */
esp_err_t audio_feedback_set_config(bool enabled, uint8_t volume);
void audio_feedback_get_status(audio_feedback_status_t *status);

/* Queues a system track (1..6) or a student greeting track (1001..1050) for
 * portal/hardware testing. */
esp_err_t audio_feedback_test_track(uint16_t track);
