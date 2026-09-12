#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "student_db.h"

#define NVS_NAMESPACE "student_db"
#define NVS_ENROLLMENT_COMPLETE_KEY "enroll_done"
#define TAG           "student_db"

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

/* Compatibility for records created by the old enroll form parser. */
static void decode_legacy_url_encoding(char *value)
{
    char *read = value;
    char *write = value;
    while (*read != '\0') {
        if (read[0] == '%' && read[1] != '\0' && read[2] != '\0') {
            int high = hex_value(read[1]);
            int low = hex_value(read[2]);
            if (high >= 0 && low >= 0) {
                *write++ = (char)((high << 4) | low);
                read += 3;
                continue;
            }
        }
        *write++ = *read++;
    }
    *write = '\0';
}

/* Key format: "s001" ... "s050" */
static void make_key(uint16_t face_id, char *key, size_t key_sz)
{
    snprintf(key, key_sz, "s%03u", (unsigned)face_id);
}

esp_err_t student_db_init(void)
{
    /* nvs_flash_init() phải được gọi trước trong app_main */
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "student_db ready (%d students)", student_db_count());
    return ESP_OK;
}

esp_err_t student_db_set(uint16_t face_id, const char *name, const char *class_name)
{
    if (face_id == 0 || face_id > STUDENT_MAX_COUNT || name == NULL ||
        class_name == NULL || name[0] == '\0' || strchr(name, '|') != NULL ||
        strchr(class_name, '|') != NULL || strlen(name) >= STUDENT_NAME_MAX ||
        strlen(class_name) >= STUDENT_CLASS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Gói thành chuỗi "name|class" cho gọn */
    char value[STUDENT_NAME_MAX + STUDENT_CLASS_MAX + 2];
    snprintf(value, sizeof(value), "%s|%s", name, class_name);

    char key[8];
    make_key(face_id, key, sizeof(key));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved student id=%u name='%s' class='%s'", face_id, name, class_name);
    }
    return err;
}

esp_err_t student_db_delete(uint16_t face_id)
{
    if (face_id == 0 || face_id > STUDENT_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    char key[8];
    make_key(face_id, key, sizeof(key));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Deleted student metadata id=%u", face_id);
    }
    return err;
}

esp_err_t student_db_get(uint16_t face_id, student_info_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    char key[8];
    make_key(face_id, key, sizeof(key));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    char value[STUDENT_NAME_MAX + STUDENT_CLASS_MAX + 2];
    size_t len = sizeof(value);
    err = nvs_get_str(h, key, value, &len);
    nvs_close(h);

    if (err != ESP_OK) return err;

    decode_legacy_url_encoding(value);

    /* Parse "name|class" */
    out->face_id = face_id;
    char *sep = strchr(value, '|');
    if (sep) {
        size_t name_len = (size_t)(sep - value);
        if (name_len >= STUDENT_NAME_MAX) name_len = STUDENT_NAME_MAX - 1;
        memcpy(out->name, value, name_len);
        out->name[name_len] = '\0';
        strncpy(out->class_name, sep + 1, STUDENT_CLASS_MAX - 1);
        out->class_name[STUDENT_CLASS_MAX - 1] = '\0';
    } else {
        strncpy(out->name, value, STUDENT_NAME_MAX - 1);
        out->name[STUDENT_NAME_MAX - 1] = '\0';
        out->class_name[0] = '\0';
    }
    return ESP_OK;
}

esp_err_t student_db_clear_all(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "student_db cleared");
    return err;
}

bool student_db_is_enrollment_complete(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t complete = 0;
    esp_err_t err = nvs_get_u8(h, NVS_ENROLLMENT_COMPLETE_KEY, &complete);
    nvs_close(h);
    return err == ESP_OK && complete == 1;
}

esp_err_t student_db_set_enrollment_complete(bool complete)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, NVS_ENROLLMENT_COMPLETE_KEY, complete ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Enrollment session marked %s", complete ? "complete" : "active");
    }
    return err;
}

int student_db_count(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;

    int count = 0;
    char key[8];
    for (uint16_t id = 1; id <= STUDENT_MAX_COUNT; id++) {
        make_key(id, key, sizeof(key));
        size_t len = 0;
        if (nvs_get_str(h, key, NULL, &len) == ESP_OK) {
            count++;
        }
    }
    nvs_close(h);
    return count;
}
