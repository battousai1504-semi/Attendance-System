#pragma once

/*
 * http_upload.h — Upload điểm danh lên Google Apps Script
 *
 * Mỗi record được ghi vào outbox SPIFFS trước, sau đó task nền gửi HTTPS.
 * Record chỉ bị xóa khỏi outbox sau khi server trả về {"ok":true}.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define HTTP_UPLOAD_EVENT_ID_MAX 48
#define HTTP_UPLOAD_DEVICE_ID_MAX 32

typedef struct {
    char     event_id[HTTP_UPLOAD_EVENT_ID_MAX];
    char     device_id[HTTP_UPLOAD_DEVICE_ID_MAX];
    uint16_t student_id;
    char     name[48];
    char     class_name[16];
    char     timestamp[24];   /* "YYYY-MM-DD HH:MM:SS" */
    int      confidence_milli; /* similarity * 1000 */
} upload_record_t;

/**
 * Khởi tạo module và tạo background task upload.
 * @param gas_url  URL deploy của Google Apps Script (https://script.google.com/...)
 */
esp_err_t http_upload_init(const char *gas_url, const char *api_token);

/**
 * Ghi bền vững một bản ghi vào outbox nhưng chưa đánh thức task upload.
 * Caller phải hoàn tất bản sao local rồi mới gọi http_upload_notify().
 * @return ESP_OK nếu đã lưu, ESP_ERR_NO_MEM nếu outbox đầy
 */
esp_err_t http_upload_stage(const upload_record_t *record);

/** Đánh thức task upload sau khi transaction local đã hoàn tất. */
void http_upload_notify(void);

/**
 * Kiểm tra số lượng bản ghi đang chờ upload.
 */
int http_upload_pending_count(void);
