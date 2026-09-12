# Google Apps Script backend

Backend này nhận bản ghi điểm danh từ ESP32 và ghi vào sheet `Attendance`.
`event_id` được dùng làm khóa idempotency nên firmware có thể retry mà không tạo
dòng trùng.

## Thiết lập

1. Tạo một Google Spreadsheet và một Apps Script project.
2. Chép `Code.gs` và `appsscript.json` vào project.
3. Trong **Project Settings → Script Properties**, thêm:
   - `SPREADSHEET_ID`: ID của spreadsheet (có thể bỏ qua nếu script được bind vào sheet).
   - `API_TOKEN`: một chuỗi bí mật dài, ngẫu nhiên.
4. Chạy hàm `setup` một lần và cấp quyền.
5. Deploy dưới dạng **Web app**, execute as owner, access phù hợp với thiết bị.
6. Chép URL deployment và token vào `firmware/main/cloud_config.h`.

Mỗi lần sửa Apps Script cần tạo deployment version mới hoặc cập nhật deployment
hiện tại. Không đưa `API_TOKEN` vào source control.
