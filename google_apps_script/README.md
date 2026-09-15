# Google Apps Script backend

Backend này nhận bản ghi điểm danh từ ESP32 và ghi vào sheet `Attendance`.
`event_id` được dùng làm khóa idempotency nên firmware có thể retry mà không tạo
dòng trùng.

## Thiết lập

1. Tạo một Google Spreadsheet và một Apps Script project.
2. Chép `Code.gs` và `appsscript.json` vào project.
3. Trong **Project Settings → Script Properties**, thêm:
   - `SPREADSHEET_ID`: ID của spreadsheet (có thể bỏ qua nếu script được bind vào sheet).
   - `API_TOKEN`: chuỗi bí mật ngẫu nhiên, ít nhất 32 ký tự, trùng chính xác
     với `CDP_GAS_TOKEN` trong firmware.
4. Chạy hàm `setup` một lần và cấp quyền. Token phải có ít nhất 32 ký tự;
   `setup` cũng kiểm tra đúng cấu trúc 9 cột của sheet `Attendance`.
5. Deploy dưới dạng **Web app**, execute as owner, chọn **Anyone** để ESP32
   truy cập mà không cần đăng nhập Google.
6. Chép URL deployment và token vào `firmware/main/cloud_config.h`.

Mỗi lần sửa Apps Script cần tạo deployment version mới hoặc cập nhật deployment
hiện tại. Không đưa `API_TOKEN` vào source control.

Mở URL `/exec` trong trình duyệt để kiểm tra deployment. Bản Phase 1 hiện tại
phải trả JSON chứa:

```json
{"ok":true,"service":"robot-attendance","api_version":"phase1-v2","configured":true,"sheet_ready":true}
```

Google Content Service trả kết quả qua redirect tới `script.googleusercontent.com`.
Client phải theo redirect; với phản hồi `301`, `302` hoặc `303` sau POST, request
kế tiếp dùng GET. Firmware đã xử lý quy tắc này trong `http_upload.c`.
