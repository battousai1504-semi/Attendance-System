#include <stdio.h>

#include "app_version.h"
#include "http_payload.h"

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool ok;
} json_writer_t;

static void json_append_char(json_writer_t *writer, char value)
{
    if (!writer->ok || writer->length + 1 >= writer->capacity) {
        writer->ok = false;
        return;
    }
    writer->buffer[writer->length++] = value;
    writer->buffer[writer->length] = '\0';
}

static void json_append_text(json_writer_t *writer, const char *text)
{
    while (*text != '\0' && writer->ok) json_append_char(writer, *text++);
}

static void json_append_string(json_writer_t *writer, const char *value)
{
    static const char hex[] = "0123456789abcdef";
    json_append_char(writer, '"');
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != '\0' && writer->ok; cursor++) {
        switch (*cursor) {
        case '"': json_append_text(writer, "\\\""); break;
        case '\\': json_append_text(writer, "\\\\"); break;
        case '\b': json_append_text(writer, "\\b"); break;
        case '\f': json_append_text(writer, "\\f"); break;
        case '\n': json_append_text(writer, "\\n"); break;
        case '\r': json_append_text(writer, "\\r"); break;
        case '\t': json_append_text(writer, "\\t"); break;
        default:
            if (*cursor < 0x20) {
                json_append_text(writer, "\\u00");
                json_append_char(writer, hex[*cursor >> 4]);
                json_append_char(writer, hex[*cursor & 0x0f]);
            } else {
                json_append_char(writer, (char)*cursor);
            }
        }
    }
    json_append_char(writer, '"');
}

bool http_payload_build_attendance(const upload_record_t *record,
                                   const char *api_token,
                                   char *buffer,
                                   size_t buffer_size)
{
    if (record == NULL || api_token == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }
    char number[32];
    json_writer_t writer = {
        .buffer = buffer,
        .capacity = buffer_size,
        .length = 0,
        .ok = true,
    };
    buffer[0] = '\0';

    snprintf(number, sizeof(number), "{\"api_version\":%d", CDP_CLOUD_API_VERSION);
    json_append_text(&writer, number);
    json_append_text(&writer, ",\"type\":\"attendance\",\"event_id\":");
    json_append_string(&writer, record->event_id);
    json_append_text(&writer, ",\"device_id\":");
    json_append_string(&writer, record->device_id);
    json_append_text(&writer, ",\"token\":");
    json_append_string(&writer, api_token);
    snprintf(number, sizeof(number), ",\"student_id\":%u", (unsigned)record->student_id);
    json_append_text(&writer, number);
    json_append_text(&writer, ",\"name\":");
    json_append_string(&writer, record->name);
    json_append_text(&writer, ",\"class\":");
    json_append_string(&writer, record->class_name);
    json_append_text(&writer, ",\"timestamp\":");
    json_append_string(&writer, record->timestamp);
    snprintf(number, sizeof(number), ",\"confidence_milli\":%d",
             record->confidence_milli);
    json_append_text(&writer, number);
    json_append_text(&writer,
                     ",\"method\":\"FACE\",\"session_id\":\"\","
                     "\"firmware_version\":");
    json_append_string(&writer, CDP_FIRMWARE_VERSION);
    json_append_char(&writer, '}');
    return writer.ok;
}

bool http_payload_build_ota_manifest_request(const char *device_id,
                                             const char *api_token,
                                             char *buffer,
                                             size_t buffer_size)
{
    if (device_id == NULL || api_token == NULL || buffer == NULL ||
        buffer_size == 0) return false;
    char prefix[48];
    json_writer_t writer = {
        .buffer = buffer,
        .capacity = buffer_size,
        .length = 0,
        .ok = true,
    };
    buffer[0] = '\0';
    snprintf(prefix, sizeof(prefix), "{\"api_version\":%d",
             CDP_CLOUD_API_VERSION);
    json_append_text(&writer, prefix);
    json_append_text(&writer, ",\"type\":\"ota_manifest\",\"device_id\":");
    json_append_string(&writer, device_id);
    json_append_text(&writer, ",\"token\":");
    json_append_string(&writer, api_token);
    json_append_text(&writer, ",\"firmware_version\":");
    json_append_string(&writer, CDP_FIRMWARE_VERSION);
    json_append_char(&writer, '}');
    return writer.ok;
}

bool http_payload_build_heartbeat(const heartbeat_payload_t *heartbeat,
                                  char *buffer,
                                  size_t buffer_size)
{
    if (heartbeat == NULL || heartbeat->device_id == NULL ||
        heartbeat->api_token == NULL || heartbeat->last_error == NULL ||
        buffer == NULL || buffer_size == 0 || heartbeat->rssi < -127 ||
        heartbeat->rssi > 0) {
        return false;
    }
    char number[224];
    json_writer_t writer = {
        .buffer = buffer,
        .capacity = buffer_size,
        .length = 0,
        .ok = true,
    };
    buffer[0] = '\0';
    snprintf(number, sizeof(number), "{\"api_version\":%d", CDP_CLOUD_API_VERSION);
    json_append_text(&writer, number);
    json_append_text(&writer, ",\"type\":\"heartbeat\",\"device_id\":");
    json_append_string(&writer, heartbeat->device_id);
    json_append_text(&writer, ",\"token\":");
    json_append_string(&writer, heartbeat->api_token);
    json_append_text(&writer, ",\"firmware_version\":");
    json_append_string(&writer, CDP_FIRMWARE_VERSION);
    snprintf(number, sizeof(number),
             ",\"uptime_seconds\":%lu,\"rssi\":%d,\"free_heap\":%lu,"
             "\"free_psram\":%lu,\"pending_events\":%lu,"
             "\"sync_latency_ms\":%lu,\"consecutive_failures\":%lu,"
             "\"last_error\":",
             (unsigned long)heartbeat->uptime_seconds, heartbeat->rssi,
             (unsigned long)heartbeat->free_heap,
             (unsigned long)heartbeat->free_psram,
             (unsigned long)heartbeat->pending_events,
             (unsigned long)heartbeat->sync_latency_ms,
             (unsigned long)heartbeat->consecutive_failures);
    json_append_text(&writer, number);
    json_append_string(&writer, heartbeat->last_error);
    json_append_char(&writer, '}');
    return writer.ok;
}
