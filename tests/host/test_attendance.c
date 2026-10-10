#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "attendance.h"

#define TEST_CURRENT "/tmp/robot_attendance_host_test.csv"
#define TEST_PREVIOUS "/tmp/robot_attendance_host_test.previous.csv"

static bool s_clock_valid;
static time_t s_now = 1760000000;
static uint32_t s_random = 0x12345678;

uint32_t esp_random(void)
{
    return s_random++;
}

void manual_clock_init(void) {}

time_t manual_clock_now(void)
{
    return s_now;
}

void manual_clock_format(char *buffer, size_t buffer_size)
{
    struct tm value;
    localtime_r(&s_now, &value);
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &value);
}

bool manual_clock_is_valid(void)
{
    return s_clock_valid;
}

static bool file_contains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) return false;
    char buffer[512] = {0};
    const size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[length] = '\0';
    return strstr(buffer, needle) != NULL;
}

int main(void)
{
    remove(TEST_CURRENT);
    remove(TEST_PREVIOUS);

    assert(attendance_init() == ESP_OK);
    attendance_entry_t event = {0};
    assert(attendance_prepare(1, "FACE", &event) == ATTENDANCE_TIME_UNAVAILABLE);

    s_clock_valid = true;
    assert(attendance_prepare(1, "FACE", &event) == ATTENDANCE_RECORDED);
    assert(event.person_id == 1);
    assert(event.event_id[0] != '\0');
    assert(attendance_commit(&event, false) == ESP_OK);
    assert(file_contains(TEST_CURRENT, event.event_id));

    s_now += 30;
    assert(attendance_prepare(1, "FACE", &event) == ATTENDANCE_DUPLICATE);
    s_now += 31;
    assert(attendance_prepare(1, "FACE", &event) == ATTENDANCE_RECORDED);
    assert(attendance_commit(&event, false) == ESP_OK);

    for (uint16_t id = 2; id <= 6; id++) {
        s_now += 61;
        assert(attendance_prepare(id, "FACE", &event) == ATTENDANCE_RECORDED);
        assert(attendance_commit(&event, false) == ESP_OK);
    }
    assert(access(TEST_PREVIOUS, F_OK) == 0);

    /* attendance_init restores cooldowns from both CSV generations. */
    assert(attendance_init() == ESP_OK);
    s_now += 10;
    assert(attendance_prepare(6, "FACE", &event) == ATTENDANCE_DUPLICATE);

    assert(attendance_clear() == ESP_OK);
    assert(access(TEST_PREVIOUS, F_OK) != 0);
    remove(TEST_CURRENT);
    puts("attendance host tests: PASS");
    return 0;
}
