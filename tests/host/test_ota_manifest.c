#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ota_manifest.h"

int main(void)
{
    ota_manifest_t manifest;
    const char *valid =
        "{\"ok\":true,\"retryable\":false,\"api_version\":2,"
        "\"available\":true,\"version\":\"2.5.1-test\","
        "\"url\":\"https://example.com/robot.bin\",\"size\":4718592,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}";
    assert(ota_manifest_parse_json(valid, "2.5.0-phase2-dev", &manifest));
    assert(manifest.available);
    assert(strcmp(manifest.version, "2.5.1-test") == 0);
    assert(manifest.size == 4718592);
    assert(manifest.sha256[0] == 0x01 && manifest.sha256[31] == 0xef);

    assert(ota_manifest_parse_json(valid, "2.5.1-test", &manifest));
    assert(!manifest.available);
    assert(ota_manifest_parse_json(
        "{\"ok\":true,\"available\":false}", "2.5.0", &manifest));
    assert(!manifest.available);
    assert(!ota_manifest_parse_json(
        "{\"ok\":true,\"available\":true,\"version\":\"x\","
        "\"url\":\"http://unsafe/firmware.bin\",\"size\":1,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",
        "2.5.0", &manifest));
    assert(!ota_manifest_parse_json(
        "{\"ok\":true,\"available\":true,\"version\":\"x\","
        "\"url\":\"https://example.com/x\",\"size\":6291457,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",
        "2.5.0", &manifest));
    assert(!ota_manifest_parse_json(
        "{\"ok\":false,\"available\":true}", "2.5.0", &manifest));
    puts("OTA manifest host tests: PASS");
    return 0;
}
