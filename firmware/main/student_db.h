#pragma once

/*
 * student_db.h — Quản lý danh sách học sinh
 *
 * Lưu trong NVS (Non-Volatile Storage):
 *   namespace "student_db"
 *   key "s<id>" → compact UTF-8 string "name|class"
 *
 * Hỗ trợ tối đa 50 học sinh (đủ cho 1 lớp học).
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define STUDENT_NAME_MAX  48
#define STUDENT_CLASS_MAX 16
#define STUDENT_MAX_COUNT 50

typedef struct {
    uint16_t face_id;               /* ID từ face_engine (1-based) */
    char     name[STUDENT_NAME_MAX];
    char     class_name[STUDENT_CLASS_MAX];
} student_info_t;

/**
 * Khởi tạo NVS namespace cho student_db.
 * Phải gọi trước khi dùng các hàm khác.
 */
esp_err_t student_db_init(void);

/**
 * Lưu hoặc cập nhật thông tin học sinh.
 * @param face_id  ID trả về từ face_engine_enroll()
 * @param name     Họ tên đầy đủ (UTF-8)
 * @param class_name Tên lớp (VD: "10A1")
 */
esp_err_t student_db_set(uint16_t face_id, const char *name, const char *class_name);

/** Delete metadata for one face ID. */
esp_err_t student_db_delete(uint16_t face_id);

/**
 * Lấy thông tin học sinh theo face_id.
 * @param face_id  ID cần tra cứu
 * @param out      Buffer nhận thông tin (do caller cấp)
 * @return ESP_OK nếu tìm thấy, ESP_ERR_NOT_FOUND nếu không có
 */
esp_err_t student_db_get(uint16_t face_id, student_info_t *out);

/**
 * Xóa toàn bộ dữ liệu học sinh trong NVS.
 * KHÔNG xóa face embeddings — gọi face_engine_clear_all() riêng.
 */
esp_err_t student_db_clear_all(void);

/**
 * Enrollment remains active across reboots until the teacher explicitly
 * completes the class enrollment session.
 */
bool student_db_is_enrollment_complete(void);
esp_err_t student_db_set_enrollment_complete(bool complete);

/**
 * Đếm số học sinh đã đăng ký.
 */
int student_db_count(void);
