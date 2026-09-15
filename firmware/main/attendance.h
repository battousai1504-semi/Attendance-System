#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

typedef enum {
    ATTENDANCE_RECORDED,
    ATTENDANCE_DUPLICATE,
    ATTENDANCE_TIME_UNAVAILABLE,
    ATTENDANCE_ERROR,
} attendance_result_t;

#define ATTENDANCE_EVENT_ID_MAX 48
#define ATTENDANCE_METHOD_MAX   16
#define ATTENDANCE_TIMESTAMP_MAX 24

typedef struct {
    char event_id[ATTENDANCE_EVENT_ID_MAX];
    char timestamp[ATTENDANCE_TIMESTAMP_MAX];
    char method[ATTENDANCE_METHOD_MAX];
    uint16_t person_id;
    time_t epoch;
} attendance_entry_t;

// The SPIFFS partition must already be mounted before calling this function.
esp_err_t attendance_init(void);

// Validates cooldown/time and prepares an event without changing persistent
// state. The caller can first persist the event in the upload outbox, then call
// attendance_commit(). This ordering closes the power-loss window where a CSV
// row existed but was never queued for cloud delivery.
attendance_result_t attendance_prepare(uint16_t person_id, const char *method,
                                       attendance_entry_t *entry_out);

// Appends a prepared event to local history and activates its cooldown.
// When durable_copy_exists is true, a failure to append the diagnostic CSV is
// degraded-but-safe because the upload outbox still owns a durable copy.
esp_err_t attendance_commit(const attendance_entry_t *entry,
                            bool durable_copy_exists);

// Deletes every attendance record while keeping enrolled identities intact.
esp_err_t attendance_clear(void);

// Returns the number of saved records. Index 0 is the oldest record.
size_t attendance_get_count(void);

// Creates compact text for the selected record, for example "ID 1 FACE" and
// its timestamp. Returns false when index is outside the available history.
bool attendance_format_at(size_t index, char *summary, size_t summary_size,
                          char *timestamp, size_t timestamp_size);

// Sends the latest records to the ESP-IDF monitor for the History screen.
void attendance_print_recent(size_t max_records);
