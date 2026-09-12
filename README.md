# Robot Attendance

Firmware điểm danh khuôn mặt cho ESP32-S3 N16R8 + OV2640. Thiết bị đăng ký
khuôn mặt qua Wi-Fi AP, nhận diện tại edge, lưu lịch sử cục bộ và đồng bộ bền
vững lên Google Sheets.

## Cấu hình local

Hai file chứa cấu hình triển khai được giữ ngoài source control:

- `firmware/main/wifi_time_config.h`: sao chép từ `wifi_time_config.h.example`.
- `firmware/main/cloud_config.h`: sao chép từ `cloud_config.h.example`.

`CDP_GAS_TOKEN` phải trùng với Script Property `API_TOKEN` của Apps Script.
Đổi cả `CDP_ENROLL_AP_PASSWORD` trước khi dùng thật. Không commit hai file local
này.

## Backend Google Sheets

Xem [google_apps_script/README.md](google_apps_script/README.md). Backend dùng
`event_id` để nhận retry an toàn; cùng một event chỉ tạo tối đa một dòng.

## Dữ liệu trên thiết bị

- NVS namespace `student_db`: tên và lớp theo face ID.
- `/spiffs/attendance.csv`: lịch sử điểm danh.
- `/spiffs/upload_outbox.bin`: các event đang chờ server xác nhận.
- Face embeddings: database ESP-DL trong partition SPIFFS `faces`.

Outbox là nguồn dữ liệu upload chuẩn: record được ghi xuống flash trước khi gửi
và chỉ bị xóa khi server trả về `{"ok":true}`.

## Build

Dự án dùng ESP-IDF 6.0.2, target `esp32s3`:

```powershell
cd firmware
idf.py build
```

Binary đầu ra: `firmware/build/robot_attendance.bin`.
