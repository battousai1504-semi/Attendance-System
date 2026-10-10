#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ota_manifest.h"

static const char *find_value(const char *json, const char *key)
{
    char needle[64];
    if (strlen(key) + 3 >= sizeof(needle)) return NULL;
    needle[0] = '"';
    strcpy(needle + 1, key);
    strcat(needle, "\"");
    const char *cursor = strstr(json, needle);
    if (cursor == NULL) return NULL;
    cursor += strlen(needle);
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor++ != ':') return NULL;
    while (isspace((unsigned char)*cursor)) cursor++;
    return cursor;
}

static bool parse_bool(const char *json, const char *key, bool *value)
{
    const char *cursor = find_value(json, key);
    if (cursor == NULL) return false;
    if (strncmp(cursor, "true", 4) == 0) {
        *value = true;
        return true;
    }
    if (strncmp(cursor, "false", 5) == 0) {
        *value = false;
        return true;
    }
    return false;
}

static bool parse_string(const char *json, const char *key,
                         char *output, size_t capacity)
{
    const char *cursor = find_value(json, key);
    if (cursor == NULL || *cursor++ != '"' || capacity == 0) return false;
    size_t length = 0;
    while (*cursor != '\0' && *cursor != '"') {
        unsigned char value = (unsigned char)*cursor++;
        if (value == '\\') {
            value = (unsigned char)*cursor++;
            if (value == '"' || value == '\\' || value == '/') {
                /* already decoded */
            } else if (value == 'b') value = '\b';
            else if (value == 'f') value = '\f';
            else if (value == 'n') value = '\n';
            else if (value == 'r') value = '\r';
            else if (value == 't') value = '\t';
            else return false;
        }
        if (value < 0x20 || length + 1 >= capacity) return false;
        output[length++] = (char)value;
    }
    if (*cursor != '"') return false;
    output[length] = '\0';
    return true;
}

static bool parse_size(const char *json, const char *key, uint32_t *value)
{
    const char *cursor = find_value(json, key);
    if (cursor == NULL || *cursor == '-') return false;
    errno = 0;
    char *end = NULL;
    const unsigned long parsed = strtoul(cursor, &end, 10);
    if (errno != 0 || end == cursor || parsed == 0 ||
        parsed > OTA_MANIFEST_MAX_IMAGE_SIZE) return false;
    while (isspace((unsigned char)*end)) end++;
    if (*end != ',' && *end != '}') return false;
    *value = (uint32_t)parsed;
    return true;
}

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool parse_sha256(const char *json, uint8_t output[32])
{
    char text[65];
    if (!parse_string(json, "sha256", text, sizeof(text)) ||
        strlen(text) != 64) return false;
    for (size_t index = 0; index < 32; index++) {
        const int high = hex_nibble(text[index * 2]);
        const int low = hex_nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

bool ota_manifest_parse_json(const char *json, const char *current_version,
                             ota_manifest_t *manifest)
{
    if (json == NULL || current_version == NULL || manifest == NULL) return false;
    memset(manifest, 0, sizeof(*manifest));
    bool ok = false;
    if (!parse_bool(json, "ok", &ok) || !ok ||
        !parse_bool(json, "available", &manifest->available)) return false;
    if (!manifest->available) return true;
    if (!parse_string(json, "version", manifest->version,
                      sizeof(manifest->version)) ||
        manifest->version[0] == '\0' ||
        !parse_string(json, "url", manifest->url, sizeof(manifest->url)) ||
        strncmp(manifest->url, "https://", 8) != 0 ||
        !parse_size(json, "size", &manifest->size) ||
        !parse_sha256(json, manifest->sha256)) {
        memset(manifest, 0, sizeof(*manifest));
        return false;
    }
    if (strcmp(manifest->version, current_version) == 0) {
        memset(manifest, 0, sizeof(*manifest));
    }
    return true;
}
