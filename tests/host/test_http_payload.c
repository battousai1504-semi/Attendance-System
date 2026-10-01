#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "http_payload.h"

int main(void)
{
    upload_record_t record = {
        .student_id = 7,
        .confidence_milli = 918,
    };
    snprintf(record.event_id, sizeof(record.event_id), "1760000000-7-abcdef01");
    snprintf(record.device_id, sizeof(record.device_id), "robot-01");
    snprintf(record.name, sizeof(record.name), "Nguyen \"A\"\\Test");
    snprintf(record.class_name, sizeof(record.class_name), "12A1");
    snprintf(record.timestamp, sizeof(record.timestamp), "2026-09-21 08:30:00");

    char json[768];
    assert(http_payload_build_attendance(
        &record, "0123456789abcdef0123456789abcdef", json, sizeof(json)));
    assert(strstr(json, "\"api_version\":2") != NULL);
    assert(strstr(json, "\"type\":\"attendance\"") != NULL);
    assert(strstr(json, "\"event_id\":\"1760000000-7-abcdef01\"") != NULL);
    assert(strstr(json, "\"name\":\"Nguyen \\\"A\\\"\\\\Test\"") != NULL);
    assert(strstr(json, "\"session_id\":\"\"") != NULL);
    assert(strstr(json, "\"firmware_version\":\"3.0.0-phase3-dev\"") != NULL);

    char too_small[32];
    assert(!http_payload_build_attendance(
        &record, "0123456789abcdef0123456789abcdef", too_small,
        sizeof(too_small)));
    assert(!http_payload_build_attendance(NULL, "token", json, sizeof(json)));

    const heartbeat_payload_t heartbeat = {
        .device_id = "robot-01",
        .api_token = "0123456789abcdef0123456789abcdef",
        .uptime_seconds = 42,
        .rssi = -61,
        .free_heap = 123456,
        .free_psram = 654321,
        .pending_events = 3,
        .sync_latency_ms = 842,
        .consecutive_failures = 2,
        .last_error = "probe \"safe\"",
    };
    assert(http_payload_build_heartbeat(&heartbeat, json, sizeof(json)));
    assert(strstr(json, "\"type\":\"heartbeat\"") != NULL);
    assert(strstr(json, "\"device_id\":\"robot-01\"") != NULL);
    assert(strstr(json, "\"rssi\":-61") != NULL);
    assert(strstr(json, "\"last_error\":\"probe \\\"safe\\\"\"") != NULL);
    assert(strstr(json, "\"sync_latency_ms\":842") != NULL);
    assert(strstr(json, "\"consecutive_failures\":2") != NULL);
    assert(http_payload_build_ota_manifest_request(
        "robot-01", "0123456789abcdef0123456789abcdef", json, sizeof(json)));
    assert(strstr(json, "\"type\":\"ota_manifest\"") != NULL);
    assert(strstr(json, "\"firmware_version\":\"3.0.0-phase3-dev\"") != NULL);

    heartbeat_payload_t invalid_heartbeat = heartbeat;
    invalid_heartbeat.rssi = 1;
    assert(!http_payload_build_heartbeat(&invalid_heartbeat, json, sizeof(json)));
    puts("HTTP payload host tests: PASS");
    return 0;
}
