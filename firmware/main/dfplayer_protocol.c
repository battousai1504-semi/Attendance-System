#include <string.h>

#include "dfplayer_protocol.h"

#define DFPLAYER_START_BYTE 0x7eU
#define DFPLAYER_VERSION    0xffU
#define DFPLAYER_LENGTH     0x06U
#define DFPLAYER_END_BYTE   0xefU

uint16_t dfplayer_protocol_checksum(const uint8_t payload[6])
{
    if (payload == NULL) return 0;
    uint16_t sum = 0;
    for (size_t index = 0; index < 6; index++) {
        sum = (uint16_t)(sum + payload[index]);
    }
    return (uint16_t)(0U - sum);
}

void dfplayer_build_frame(uint8_t command, uint16_t parameter,
                          bool request_feedback,
                          uint8_t output[DFPLAYER_FRAME_SIZE])
{
    if (output == NULL) return;
    output[0] = DFPLAYER_START_BYTE;
    output[1] = DFPLAYER_VERSION;
    output[2] = DFPLAYER_LENGTH;
    output[3] = command;
    output[4] = request_feedback ? 1U : 0U;
    output[5] = (uint8_t)(parameter >> 8);
    output[6] = (uint8_t)(parameter & 0xffU);
    const uint16_t checksum = dfplayer_protocol_checksum(&output[1]);
    output[7] = (uint8_t)(checksum >> 8);
    output[8] = (uint8_t)(checksum & 0xffU);
    output[9] = DFPLAYER_END_BYTE;
}

bool dfplayer_parse_frame(const uint8_t *data, size_t length,
                          dfplayer_frame_t *frame)
{
    if (data == NULL || frame == NULL || length != DFPLAYER_FRAME_SIZE ||
        data[0] != DFPLAYER_START_BYTE || data[1] != DFPLAYER_VERSION ||
        data[2] != DFPLAYER_LENGTH || data[9] != DFPLAYER_END_BYTE ||
        data[4] > 1U) {
        return false;
    }
    const uint16_t expected = dfplayer_protocol_checksum(&data[1]);
    const uint16_t received = (uint16_t)(((uint16_t)data[7] << 8) | data[8]);
    if (expected != received) return false;

    memset(frame, 0, sizeof(*frame));
    frame->command = data[3];
    frame->feedback_requested = data[4] != 0;
    frame->parameter = (uint16_t)(((uint16_t)data[5] << 8) | data[6]);
    return true;
}
