#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_MANIFEST_VERSION_MAX 32
#define OTA_MANIFEST_URL_MAX 512
#define OTA_MANIFEST_SHA256_SIZE 32
#define OTA_MANIFEST_MAX_IMAGE_SIZE 0x600000U

typedef struct {
    bool available;
    char version[OTA_MANIFEST_VERSION_MAX];
    char url[OTA_MANIFEST_URL_MAX];
    uint32_t size;
    uint8_t sha256[OTA_MANIFEST_SHA256_SIZE];
} ota_manifest_t;

/* Strict parser for the authenticated Apps Script OTA response. */
bool ota_manifest_parse_json(const char *json, const char *current_version,
                             ota_manifest_t *manifest);
