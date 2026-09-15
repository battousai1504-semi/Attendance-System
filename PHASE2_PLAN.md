# Kế hoạch Phase 2 — Classroom Attendance Product

Ngày soạn: 2026-09-16

## 1. Trạng thái đầu vào

Phase 1 đã hoàn thành luồng chức năng chính trên board ESP32-S3 N16R8 với hai
học sinh:

- đăng ký khuôn mặt và metadata học sinh;
- nhận diện đúng hai học sinh;
- chặn điểm danh lặp trong thời gian cooldown;
- lưu sự kiện cục bộ;
- gửi HTTPS qua Apps Script và ghi đúng vào Google Sheets;
- firmware build thành công bằng ESP-IDF 6.0.2 trên Ubuntu.

Phase 2 không thay thuật toán nhận diện cốt lõi. Mục tiêu là biến MVP một thiết
bị thành hệ thống điểm danh lớp học có thể cấu hình, theo dõi và trình diễn như
một sản phẩm hoàn chỉnh.

## 2. Mục tiêu sản phẩm

Sau Phase 2, giáo viên có thể:

1. Tạo một buổi điểm danh cho lớp và tiết học.
2. Xem số học sinh có mặt, đi muộn và vắng theo thời gian thực.
3. Tra cứu lịch sử theo ngày, lớp và học sinh.
4. Sửa một bản ghi bằng thao tác có lưu vết, không sửa dữ liệu gốc âm thầm.
5. Cấu hình Wi-Fi và cloud cho thiết bị mà không biên dịch lại firmware.
6. Biết thiết bị nào đang online, phiên bản firmware và số event đang chờ gửi.
7. Cập nhật firmware an toàn và tự quay lại bản cũ nếu bản mới không boot được.

## 3. Kiến trúc đích

```text
ESP32-S3 camera
  ├─ Face recognition tại thiết bị
  ├─ NVS: cấu hình + metadata học sinh
  ├─ SPIFFS: face DB + attendance/outbox
  ├─ API v2: attendance + heartbeat + active session
  └─ OTA: hai phân vùng ứng dụng + rollback
                  │ HTTPS
                  ▼
Google Apps Script API v2
  ├─ xác thực từng thiết bị
  ├─ kiểm tra schema + idempotency
  ├─ quản lý buổi học
  ├─ ghi audit log
  └─ phục vụ dashboard giáo viên
                  │
                  ▼
Google Sheets
  ├─ Attendance
  ├─ Students
  ├─ Classes
  ├─ Sessions
  ├─ Devices
  └─ AuditLog
```

Ảnh khuôn mặt và face embedding tiếp tục nằm trên thiết bị. Phase 2 không đưa
dữ liệu sinh trắc học lên Google Sheets hoặc Apps Script.

## 4. Phạm vi triển khai

### P2.0 — Đóng băng baseline Phase 1

Mục đích: tạo điểm quay lại sạch trước khi thay đổi partition, API và cấu hình.

Công việc:

- chạy lại host tests, Apps Script tests và `idf.py build`;
- lưu log test thật của hai học sinh, che SSID, password, token và URL deploy;
- kiểm tra `wifi_time_config.h` và `cloud_config.h` vẫn bị Git ignore;
- cập nhật version từ release candidate thành bản Phase 1 ổn định;
- commit và tag `v1.2.1-phase1`;
- sửa đường vào maintenance: GPIO0/BOOT được đọc trong một cửa sổ khi ứng dụng
  đã chạy hoặc dùng cơ chế double-reset. Không yêu cầu giữ BOOT trong lúc reset
  vì thao tác đó có thể đưa ESP32-S3 vào ROM download mode.

Tiêu chí hoàn thành:

- checkout tag Phase 1 có thể build lại trên Ubuntu;
- không có secret trong Git;
- flash thường giữ nguyên hai học sinh và lịch sử;
- có một cách vào maintenance được test thật và ghi đúng trong tài liệu.

### P2.1 — Data model và Apps Script API v2

Mục đích: chuyển từ một bảng event đơn lẻ sang dữ liệu lớp học có cấu trúc.

Công việc:

- thêm các sheet `Students`, `Classes`, `Sessions`, `Devices`, `AuditLog`;
- giữ `Attendance` là nhật ký sự kiện gốc, chỉ append;
- thêm `session_id`, `firmware_version` và thời gian nhận server vào event;
- định nghĩa versioned request/response (`api_version: 2`);
- route `doPost` theo loại request: `attendance`, `heartbeat`, `session_sync`;
- migration không làm mất chín cột dữ liệu Phase 1;
- thêm khóa theo từng `device_id`, trạng thái enable/disable và thời điểm xoay
  khóa;
- bổ sung integration test cho token sai, schema sai, duplicate, server busy,
  redirect và event cũ được retry.

Tiêu chí hoàn thành:

- migration chạy hai lần vẫn cho cùng kết quả;
- một `event_id` chỉ tạo một dòng;
- request sai không làm thay đổi sheet;
- API trả mã lỗi ổn định và cho biết lỗi nào được retry;
- dữ liệu Phase 1 vẫn đọc được sau migration.

### P2.2 — Buổi học và dashboard giáo viên

Mục đích: trả lời được câu hỏi thực tế “ai có mặt, ai vắng trong tiết này?”.

Công việc:

- tạo/lựa chọn buổi học theo lớp, ngày, tiết và giờ bắt đầu;
- ánh xạ attendance event vào `session_id` đang hoạt động;
- tính trạng thái `Có mặt`, `Đi muộn`, `Vắng`, `Có phép` từ roster và event;
- dashboard responsive bằng Apps Script HTML Service;
- bộ lọc ngày, lớp, buổi học và học sinh;
- xuất CSV cho một buổi học;
- thao tác điều chỉnh thủ công ghi thêm vào `AuditLog` gồm người sửa, thời gian,
  lý do và giá trị trước/sau.

Tiêu chí hoàn thành:

- dashboard tổng hợp đúng cho ít nhất hai buổi học và hai học sinh;
- số có mặt + đi muộn + vắng + có phép bằng sĩ số buổi học;
- event gốc không bị sửa hoặc xóa khi giáo viên điều chỉnh;
- dashboard dùng được trên điện thoại và laptop.

### P2.3 — Provisioning và quản lý cấu hình thiết bị

Mục đích: bỏ Wi-Fi, token và URL cloud khỏi header phải biên dịch riêng.

Công việc:

- lưu cấu hình runtime trong NVS với schema version và checksum;
- mở setup portal có mật khẩu từ maintenance mode;
- quét/chọn Wi-Fi 2,4 GHz, nhập endpoint, device ID và device secret;
- test kết nối trước khi lưu cấu hình mới;
- giữ cấu hình cũ nếu test thất bại hoặc mất điện giữa lúc cập nhật;
- hỗ trợ factory reset có xác nhận rõ phạm vi xóa;
- không in credential hoặc secret ra serial log và HTTP response.

Tiêu chí hoàn thành:

- đổi Wi-Fi và endpoint mà không rebuild firmware;
- reboot vẫn giữ cấu hình;
- nhập sai Wi-Fi không làm mất cấu hình đang hoạt động;
- firmware từ repository build được mà không cần secret local.

### P2.4 — Device health và độ bền vận hành

Mục đích: biết thiết bị đang khỏe hay đang mất dữ liệu trước khi giáo viên phát
hiện bằng mắt.

Công việc:

- heartbeat gồm uptime, RSSI, free heap, free PSRAM, firmware version, thời gian
  sync cuối và số event pending;
- sheet `Devices` hiển thị last seen và trạng thái online/offline;
- exponential backoff có jitter cho upload và heartbeat;
- đo queue depth, sync latency và số lỗi liên tiếp;
- CRC cho record outbox hoặc định dạng record mới có version + checksum;
- fault-injection tests: tắt Wi-Fi, reboot, reset giữa lúc ghi, timeout, 5xx,
  token sai và outbox gần đầy.

Tiêu chí hoàn thành:

- event offline sống qua reboot và chỉ xuất hiện một lần trên cloud;
- heartbeat không chặn nhận diện;
- dashboard báo offline sau ngưỡng cấu hình;
- lỗi một event không làm treo vĩnh viễn các event phía sau mà không có cảnh báo.

### P2.5 — OTA có rollback

Mục đích: cập nhật thiết bị mà không cần cắm cáp cho mỗi phiên bản.

Công việc:

- đổi partition table sang `ota_0`, `ota_1`, `otadata` và vùng dữ liệu;
- tạo manifest gồm version, URL, kích thước và SHA-256;
- tải image qua HTTPS, xác minh hash rồi mới boot partition mới;
- đánh dấu image hợp lệ chỉ sau khi camera, PSRAM, NVS và SPIFFS khởi tạo;
- tự rollback nếu boot lỗi hoặc health check không đạt;
- CI tạo binary, checksum và release artifact.

Tiêu chí hoàn thành:

- nâng cấp và hạ phiên bản qua OTA thành công;
- ngắt nguồn giữa lúc tải không làm brick board;
- image sai checksum bị từ chối;
- image boot lỗi tự quay về phiên bản trước.

## 5. Lịch triển khai đề xuất

| Ngày | Mục tiêu | Sản phẩm cuối ngày |
|---|---|---|
| 1 | P2.0 baseline + thiết kế schema/API v2 | Tag Phase 1, ADR và migration draft |
| 2 | P2.1 Apps Script API v2 | Contract tests và API chạy trên sheet test |
| 3 | P2.2 session model + dashboard bản đầu | Tạo buổi học và xem present/absent |
| 4 | P2.2 dashboard hoàn chỉnh + audit | Bộ lọc, CSV và manual correction |
| 5 | P2.3 provisioning | Đổi Wi-Fi/cloud không cần rebuild |
| 6 | P2.4 health + fault tests | Device status và báo cáo test lỗi |
| 7 | P2.5 partition + OTA happy path | OTA lên bản mới thành công |
| 8 | OTA rollback + full regression | Báo cáo nghiệm thu Phase 2 |
| 9 | README, demo video, sơ đồ kiến trúc, CV | Hồ sơ dự án có thể công khai |

Đây là lịch tập trung. Nếu chỉ làm buổi tối, nên chia thành hai sprint, mỗi sprint
5–7 ngày:

- Sprint A: P2.0 đến P2.3.
- Sprint B: P2.4 đến P2.5 và hồ sơ dự án.

## 6. Thứ tự ưu tiên

### Bắt buộc để gọi là Phase 2 hoàn thành

- baseline Phase 1 có tag và không lộ secret;
- data model/API v2;
- session attendance và dashboard present/absent;
- provisioning không cần rebuild;
- heartbeat và offline retry test;
- tài liệu, test report và demo.

### Nên có

- audit log cho chỉnh sửa thủ công;
- per-device secret và key rotation;
- OTA rollback.

### Để Phase 3

- nhiều board chia sẻ hoặc đồng bộ face embeddings;
- ứng dụng mobile native;
- server riêng, PostgreSQL hoặc Firebase;
- secure boot và flash encryption trên board dùng để phát triển;
- nhận diện chống ảnh chụp/liveness nâng cao;
- đồng bộ ảnh hoặc dữ liệu sinh trắc học lên cloud.

## 7. Chiến lược test

Mỗi milestone chỉ được merge khi đạt bốn lớp kiểm tra phù hợp:

1. Unit test cho parser, validation, migration và tính trạng thái.
2. Contract test giữa firmware payload và Apps Script response.
3. Integration test trên một sheet test, không dùng sheet dữ liệu thật.
4. Hardware acceptance trên ESP32-S3 với log và Event ID đối chiếu được.

Ma trận regression tối thiểu cuối Phase 2:

- hai học sinh, hai lớp, hai buổi học;
- đúng người, người lạ, không có mặt và nhiều mặt;
- cùng người trong/ngoài cooldown;
- mất Wi-Fi, mất điện và server timeout;
- token sai, device bị disable và payload sai schema;
- queue tồn tại qua reboot;
- OTA thành công, image lỗi và rollback;
- dữ liệu dashboard khớp từng Event ID trong log thiết bị.

## 8. Tài liệu và nội dung CV

Deliverable công khai cuối Phase 2:

- README có ảnh board thật, sơ đồ kiến trúc và video demo 60–90 giây;
- bảng feature và các quyết định kỹ thuật chính;
- test report với tình huống offline, idempotency và OTA rollback;
- diagram luồng enrollment, recognition và cloud sync;
- release notes và tag cho Phase 1/Phase 2;
- không có Wi-Fi credential, API token, deployment URL riêng hoặc dữ liệu sinh
  trắc học trong repository.

Gợi ý mô tả CV:

> Built an ESP32-S3 edge face-attendance system with durable offline event
> delivery, idempotent Google Apps Script APIs, classroom session dashboards,
> runtime provisioning, device health monitoring, and rollback-safe OTA.

## 9. Quyết định cần chốt trước khi code Phase 2

Ngày đầu tiên chỉ cần chốt ba quyết định:

1. Dashboard tiếp tục dùng Apps Script HTML Service hay tách frontend riêng.
   Khuyến nghị Phase 2 dùng Apps Script để giữ phạm vi vừa sức.
2. Một thiết bị phục vụ một lớp hay nhiều lớp. Khuyến nghị hỗ trợ nhiều lớp
   nhưng chỉ một session hoạt động trên mỗi thiết bị.
3. OTA có nằm trong tiêu chí bắt buộc hay chuyển sang Phase 2.1. Khuyến nghị
   làm sau dashboard/provisioning để không thay partition quá sớm.

## 10. Điểm bắt đầu vào ngày mai

Buổi đầu tiên thực hiện theo thứ tự:

1. Chụp lại log Phase 1 đã đạt và làm sạch repository.
2. Sửa/test đường vào maintenance bằng BOOT trong lúc app đang chạy.
3. Commit và tag baseline Phase 1.
4. Viết `API_V2_CONTRACT.md` và schema sáu sheet.
5. Tạo một spreadsheet staging riêng cho Phase 2.
6. Viết migration và test trước khi thay đổi firmware payload.

Không thay partition table và provisioning trong ngày đầu. Baseline và API
contract phải ổn định trước để các thay đổi sau có điểm đối chiếu.
