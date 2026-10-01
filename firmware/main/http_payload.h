#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "http_upload.h"

typedef struct {
    const char *device_id;
    const char *api_token;
    uint32_t uptime_seconds;
    int rssi;
    uint32_t free_heap;
    uint32_t free_psram;
    uint32_t pending_events;
    uint32_t sync_latency_ms;
    uint32_t consecutive_failures;
    const char *last_error;
} heartbeat_payload_t;

/* Pure serializer used by the uploader and host contract tests. */
bool http_payload_build_attendance(const upload_record_t *record,
                                   const char *api_token,
                                   char *buffer,
                                   size_t buffer_size);

bool http_payload_build_heartbeat(const heartbeat_payload_t *heartbeat,
                                  char *buffer,
                                  size_t buffer_size);

bool http_payload_build_ota_manifest_request(const char *device_id,
                                             const char *api_token,
                                             char *buffer,
                                             size_t buffer_size);
