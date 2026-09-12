#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "manual_clock.h"

void manual_clock_init(void)
{
    setenv("TZ", "ICT-7", 1);
    tzset();
}

time_t manual_clock_now(void)
{
    return time(NULL);
}

void manual_clock_format(char *buffer, size_t buffer_size)
{
    const time_t now = manual_clock_now();
    const struct tm *local_time = localtime(&now);
    if (local_time != NULL) {
        strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", local_time);
    }
}

bool manual_clock_is_valid(void)
{
    const time_t now = manual_clock_now();
    struct tm local_time = {0};
    localtime_r(&now, &local_time);
    return local_time.tm_year >= (2024 - 1900);
}
