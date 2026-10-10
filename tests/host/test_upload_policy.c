#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "upload_policy.h"

static void assert_range(uint32_t failure, uint32_t nominal)
{
    const uint32_t low = nominal - nominal / 4U;
    uint32_t high = nominal + nominal / 4U;
    if (high > UPLOAD_BACKOFF_MAX_MS) high = UPLOAD_BACKOFF_MAX_MS;
    const uint32_t first = upload_policy_backoff_ms(failure, 0);
    const uint32_t second = upload_policy_backoff_ms(failure, UINT32_MAX);
    assert(first >= low && first <= high);
    assert(second >= low && second <= high);
}

int main(void)
{
    assert(upload_policy_backoff_ms(0, 123) == 0);
    assert_range(1, 5000);
    assert_range(2, 10000);
    assert_range(3, 20000);
    assert_range(6, 160000);
    assert_range(7, 300000);
    assert(upload_policy_backoff_ms(100, UINT32_MAX) <= UPLOAD_BACKOFF_MAX_MS);
    puts("Upload backoff policy tests: PASS");
    return 0;
}
