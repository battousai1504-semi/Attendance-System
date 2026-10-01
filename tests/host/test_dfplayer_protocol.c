#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dfplayer_protocol.h"

static void test_volume_frame(void)
{
    uint8_t frame[DFPLAYER_FRAME_SIZE] = {0};
    const uint8_t expected[DFPLAYER_FRAME_SIZE] = {
        0x7e, 0xff, 0x06, 0x06, 0x01, 0x00, 0x12, 0xfe, 0xe2, 0xef,
    };
    dfplayer_build_frame(0x06, 18, true, frame);
    assert(memcmp(frame, expected, sizeof(expected)) == 0);

    dfplayer_frame_t parsed = {0};
    assert(dfplayer_parse_frame(frame, sizeof(frame), &parsed));
    assert(parsed.command == 0x06);
    assert(parsed.feedback_requested);
    assert(parsed.parameter == 18);
}

static void test_play_mp3_frame(void)
{
    uint8_t frame[DFPLAYER_FRAME_SIZE] = {0};
    const uint8_t expected[DFPLAYER_FRAME_SIZE] = {
        0x7e, 0xff, 0x06, 0x12, 0x00, 0x00, 0x02, 0xfe, 0xe7, 0xef,
    };
    dfplayer_build_frame(0x12, 2, false, frame);
    assert(memcmp(frame, expected, sizeof(expected)) == 0);
}

static void test_rejects_corruption_and_partial_frames(void)
{
    uint8_t frame[DFPLAYER_FRAME_SIZE] = {0};
    dfplayer_build_frame(0x12, 6, true, frame);
    dfplayer_frame_t parsed = {0};
    assert(!dfplayer_parse_frame(frame, sizeof(frame) - 1, &parsed));

    frame[8] ^= 0x01;
    assert(!dfplayer_parse_frame(frame, sizeof(frame), &parsed));
    frame[8] ^= 0x01;
    frame[0] = 0x00;
    assert(!dfplayer_parse_frame(frame, sizeof(frame), &parsed));
}

int main(void)
{
    test_volume_frame();
    test_play_mp3_frame();
    test_rejects_corruption_and_partial_frames();
    puts("DFPlayer protocol host tests: PASS");
    return 0;
}
