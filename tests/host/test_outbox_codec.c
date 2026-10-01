#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "outbox_codec.h"

static upload_record_t sample_record(void)
{
    upload_record_t record = {0};
    snprintf(record.event_id, sizeof(record.event_id), "1790000000-1-deadbeef");
    snprintf(record.device_id, sizeof(record.device_id), "robot-01");
    record.student_id = 1;
    snprintf(record.name, sizeof(record.name), "Student One");
    snprintf(record.class_name, sizeof(record.class_name), "12A1");
    snprintf(record.timestamp, sizeof(record.timestamp), "2026-09-25 08:00:00");
    record.confidence_milli = 875;
    return record;
}

int main(void)
{
    static const char known[] = "123456789";
    assert(outbox_crc32(known, strlen(known)) == 0xcbf43926U);

    const upload_record_t source = sample_record();
    uint8_t encoded[OUTBOX_RECORD_V2_SIZE] = {0};
    size_t encoded_size = 0;
    assert(outbox_encode_v2(&source, encoded, sizeof(encoded), &encoded_size));
    assert(encoded_size == OUTBOX_RECORD_V2_SIZE);

    upload_record_t decoded = {0};
    size_t consumed = 0;
    bool legacy = true;
    assert(outbox_decode(encoded, encoded_size, &decoded, &consumed, &legacy) ==
           OUTBOX_DECODE_OK);
    assert(!legacy);
    assert(consumed == encoded_size);
    assert(memcmp(&decoded, &source, sizeof(source)) == 0);

    uint8_t corrupted[OUTBOX_RECORD_V2_SIZE];
    memcpy(corrupted, encoded, sizeof(corrupted));
    corrupted[sizeof(outbox_record_header_t) + 7] ^= 0x40;
    assert(outbox_decode(corrupted, sizeof(corrupted), &decoded,
                         &consumed, &legacy) == OUTBOX_DECODE_INVALID);
    assert(consumed == OUTBOX_RECORD_V2_SIZE);

    assert(outbox_decode(encoded, encoded_size - 1, &decoded,
                         &consumed, &legacy) == OUTBOX_DECODE_NEED_MORE);

    uint8_t legacy_bytes[OUTBOX_RECORD_V1_SIZE] = {0};
    const outbox_record_header_t legacy_header = {
        .magic = OUTBOX_RECORD_MAGIC,
        .version = OUTBOX_RECORD_VERSION_V1,
        .record_size = sizeof(upload_record_t),
    };
    memcpy(legacy_bytes, &legacy_header, sizeof(legacy_header));
    memcpy(legacy_bytes + sizeof(legacy_header), &source, sizeof(source));
    assert(outbox_decode(legacy_bytes, sizeof(legacy_bytes), &decoded,
                         &consumed, &legacy) == OUTBOX_DECODE_OK);
    assert(legacy);
    assert(strcmp(decoded.event_id, source.event_id) == 0);

    upload_record_t invalid = source;
    memset(invalid.event_id, 'x', sizeof(invalid.event_id));
    assert(!outbox_upload_record_valid(&invalid));
    puts("Outbox codec and corruption tests: PASS");
    return 0;
}
