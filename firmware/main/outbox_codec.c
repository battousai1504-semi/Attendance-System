#include <string.h>

#include "outbox_codec.h"

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t index = 0; index < length; index++) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return crc;
}

uint32_t outbox_crc32(const void *data, size_t length)
{
    if (data == NULL && length != 0) return 0;
    return crc32_update(0xffffffffU, (const uint8_t *)data, length) ^ 0xffffffffU;
}

static uint32_t outbox_checksum(const outbox_record_header_t *header,
                                const upload_record_t *record)
{
    uint32_t crc = 0xffffffffU;
    crc = crc32_update(crc, (const uint8_t *)&header->version,
                       sizeof(header->version));
    crc = crc32_update(crc, (const uint8_t *)&header->record_size,
                       sizeof(header->record_size));
    crc = crc32_update(crc, (const uint8_t *)record, sizeof(*record));
    return crc ^ 0xffffffffU;
}

bool outbox_upload_record_valid(const upload_record_t *record)
{
    return record != NULL && record->event_id[0] != '\0' &&
           record->device_id[0] != '\0' && record->student_id != 0 &&
           record->name[0] != '\0' && record->timestamp[0] != '\0' &&
           record->confidence_milli >= 0 && record->confidence_milli <= 1000 &&
           memchr(record->event_id, '\0', sizeof(record->event_id)) != NULL &&
           memchr(record->device_id, '\0', sizeof(record->device_id)) != NULL &&
           memchr(record->name, '\0', sizeof(record->name)) != NULL &&
           memchr(record->class_name, '\0', sizeof(record->class_name)) != NULL &&
           memchr(record->timestamp, '\0', sizeof(record->timestamp)) != NULL;
}

size_t outbox_encoded_size_for_version(uint16_t version)
{
    if (version == OUTBOX_RECORD_VERSION_V1) return OUTBOX_RECORD_V1_SIZE;
    if (version == OUTBOX_RECORD_VERSION_V2) return OUTBOX_RECORD_V2_SIZE;
    return 0;
}

bool outbox_encode_v2(const upload_record_t *record, uint8_t *buffer,
                      size_t capacity, size_t *encoded_size)
{
    if (!outbox_upload_record_valid(record) || buffer == NULL ||
        capacity < OUTBOX_RECORD_V2_SIZE) {
        return false;
    }
    const outbox_record_header_t header = {
        .magic = OUTBOX_RECORD_MAGIC,
        .version = OUTBOX_RECORD_VERSION_V2,
        .record_size = sizeof(upload_record_t),
    };
    const uint32_t checksum = outbox_checksum(&header, record);
    size_t offset = 0;
    memcpy(buffer + offset, &header, sizeof(header));
    offset += sizeof(header);
    memcpy(buffer + offset, record, sizeof(*record));
    offset += sizeof(*record);
    memcpy(buffer + offset, &checksum, sizeof(checksum));
    offset += sizeof(checksum);
    if (encoded_size != NULL) *encoded_size = offset;
    return true;
}

outbox_decode_result_t outbox_decode(const uint8_t *buffer, size_t length,
                                     upload_record_t *record,
                                     size_t *consumed, bool *legacy)
{
    if (consumed != NULL) *consumed = 0;
    if (legacy != NULL) *legacy = false;
    if (buffer == NULL || record == NULL) return OUTBOX_DECODE_INVALID;
    if (length < sizeof(outbox_record_header_t)) return OUTBOX_DECODE_NEED_MORE;

    outbox_record_header_t header;
    memcpy(&header, buffer, sizeof(header));
    const size_t expected = outbox_encoded_size_for_version(header.version);
    if (header.magic != OUTBOX_RECORD_MAGIC || expected == 0 ||
        header.record_size != sizeof(upload_record_t)) {
        if (consumed != NULL) *consumed = 1;
        return OUTBOX_DECODE_INVALID;
    }
    if (length < expected) return OUTBOX_DECODE_NEED_MORE;

    upload_record_t decoded;
    memcpy(&decoded, buffer + sizeof(header), sizeof(decoded));
    if (header.version == OUTBOX_RECORD_VERSION_V2) {
        uint32_t stored_checksum;
        memcpy(&stored_checksum,
               buffer + sizeof(header) + sizeof(decoded),
               sizeof(stored_checksum));
        if (stored_checksum != outbox_checksum(&header, &decoded)) {
            if (consumed != NULL) *consumed = expected;
            return OUTBOX_DECODE_INVALID;
        }
    }
    if (!outbox_upload_record_valid(&decoded)) {
        if (consumed != NULL) *consumed = expected;
        return OUTBOX_DECODE_INVALID;
    }
    *record = decoded;
    if (consumed != NULL) *consumed = expected;
    if (legacy != NULL) *legacy = header.version == OUTBOX_RECORD_VERSION_V1;
    return OUTBOX_DECODE_OK;
}
