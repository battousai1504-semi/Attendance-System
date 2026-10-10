#pragma once

/*
 * enroll_server.h — Maintenance Web UI
 *
 * Khi gọi enroll_server_start(), ESP32-S3 tạo Wi-Fi AP+STA tạm thời.
 * Giáo viên kết nối vào AP và mở http://192.168.4.1 để:
 *   1. Nhập tên học sinh và lớp
 *   2. Robot chụp ảnh và thực hiện face_engine_enroll()
 *   3. Quét/test/lưu runtime Wi-Fi và cloud configuration
 *   4. Rollback hoặc reset riêng cấu hình mạng/cloud
 *
 * Sau khi giáo viên bấm "Done", gọi enroll_server_stop() để
 * tắt AP và chuyển robot về chế độ điểm danh WiFi Station.
 */

#include <stdbool.h>
#include "esp_err.h"

/**
 * Khởi động HTTP server ở chế độ WiFi AP.
 * @param ap_ssid  SSID của hotspot tạm thời
 * @param ap_pass  Mật khẩu (NULL = open)
 */
esp_err_t enroll_server_start(const char *ap_ssid, const char *ap_pass);

/**
 * Dừng HTTP server và xóa WiFi AP.
 */
esp_err_t enroll_server_stop(void);

/**
 * Kiểm tra xem giáo viên đã bấm "Done" chưa.
 */
bool enroll_server_done(void);

/** True when configuration save/rollback/reset requires a clean reboot. */
bool enroll_server_restart_requested(void);
