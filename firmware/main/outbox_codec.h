#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "http_upload.h"

#define OUTBOX_RECORD_MAGIC       0x4154544EU
#define OUTBOX_RECORD_VERSION_V1  1U
#define OUTBOX_RECORD_VERSION_V2  2U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
} outbox_record_header_t;

typedef enum {
    OUTBOX_DECODE_OK,
    OUTBOX_DECODE_NEED_MORE,
    OUTBOX_DECODE_INVALID,
} outbox_decode_result_t;

#define OUTBOX_RECORD_V1_SIZE \
    (sizeof(outbox_record_header_t) + sizeof(upload_record_t))
#define OUTBOX_RECORD_V2_SIZE \
    (OUTBOX_RECORD_V1_SIZE + sizeof(uint32_t))

bool outbox_upload_record_valid(const upload_record_t *record);
uint32_t outbox_crc32(const void *data, size_t length);
size_t outbox_encoded_size_for_version(uint16_t version);
bool outbox_encode_v2(const upload_record_t *record, uint8_t *buffer,
                      size_t capacity, size_t *encoded_size);
outbox_decode_result_t outbox_decode(const uint8_t *buffer, size_t length,
                                     upload_record_t *record,
                                     size_t *consumed, bool *legacy);
