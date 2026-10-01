#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "device_config.h"

static device_config_values_t valid_values(void)
{
    device_config_values_t values = {0};
    snprintf(values.wifi_ssid, sizeof(values.wifi_ssid), "School WiFi");
    snprintf(values.wifi_password, sizeof(values.wifi_password), "wifi-password");
    snprintf(values.gas_url, sizeof(values.gas_url),
             "https://script.google.com/macros/s/deployment-id/exec");
    snprintf(values.device_id, sizeof(values.device_id), "robot-01");
    snprintf(values.device_secret, sizeof(values.device_secret),
             "0123456789abcdef0123456789abcdef");
    return values;
}

int main(void)
{
    device_config_values_t values = valid_values();
    assert(device_config_values_valid(&values));

    device_config_record_t record;
    assert(device_config_record_prepare(&values, 7, &record));
    assert(device_config_record_valid(&record));
    assert(record.schema_version == DEVICE_CONFIG_SCHEMA_VERSION);
    assert(record.generation == 7);

    device_config_record_t corrupt = record;
    corrupt.values.wifi_ssid[0] ^= 1;
    assert(!device_config_record_valid(&corrupt));
    corrupt = record;
    corrupt.schema_version++;
    assert(!device_config_record_valid(&corrupt));

    device_config_values_t invalid = values;
    snprintf(invalid.gas_url, sizeof(invalid.gas_url), "http://example.test/exec");
    assert(!device_config_values_valid(&invalid));
    invalid = values;
    snprintf(invalid.gas_url, sizeof(invalid.gas_url),
             "https://script.google.com/macros/s/deployment-id/exec-bad");
    assert(!device_config_values_valid(&invalid));
    invalid = values;
    snprintf(invalid.device_secret, sizeof(invalid.device_secret), "too-short");
    assert(!device_config_values_valid(&invalid));
    invalid = values;
    snprintf(invalid.device_id, sizeof(invalid.device_id), "robot 01");
    assert(!device_config_values_valid(&invalid));

    /* A fully valid requested active slot wins even when an inactive pending
     * candidate has a higher generation. This is the atomic power-loss rule. */
    assert(device_config_choose_slot(0, true, 5, true, 6) == 0);
    assert(device_config_choose_slot(1, true, 5, true, 6) == 1);
    assert(device_config_choose_slot(0, false, 5, true, 6) == 1);
    assert(device_config_choose_slot(DEVICE_CONFIG_SLOT_NONE,
                                     true, 5, true, 6) == 1);
    assert(device_config_choose_slot(DEVICE_CONFIG_SLOT_NONE,
                                     false, 0, false, 0) ==
           DEVICE_CONFIG_SLOT_NONE);

    puts("device config codec host tests: PASS");
    return 0;
}
