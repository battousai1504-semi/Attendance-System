#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DFPLAYER_FRAME_SIZE 10U

typedef struct {
    uint8_t command;
    bool feedback_requested;
    uint16_t parameter;
} dfplayer_frame_t;

/* Builds the native 10-byte DFPlayer serial frame:
 * 7E FF 06 CMD FEEDBACK PARAM_H PARAM_L CHECKSUM_H CHECKSUM_L EF. */
void dfplayer_build_frame(uint8_t command, uint16_t parameter,
                          bool request_feedback,
                          uint8_t output[DFPLAYER_FRAME_SIZE]);

/* Validates markers, protocol constants and the two's-complement checksum.
 * The caller is responsible for extracting one aligned 10-byte frame from a
 * UART stream. */
bool dfplayer_parse_frame(const uint8_t *data, size_t length,
                          dfplayer_frame_t *frame);

/* Exposed for deterministic host tests and UART stream diagnostics. The input
 * is the six payload bytes from VERSION through PARAM_L. */
uint16_t dfplayer_protocol_checksum(const uint8_t payload[6]);
