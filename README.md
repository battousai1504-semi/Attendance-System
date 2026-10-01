# Robot Attendance

Firmware điểm danh khuôn mặt cho board ESP32-S3-WROOM N16R8 camera tích hợp
(pinout kiểu Freenove/ESP32S3_EYE). Thiết bị đăng ký
khuôn mặt qua Wi-Fi AP, nhận diện tại edge, lưu lịch sử cục bộ và đồng bộ bền
vững lên Google Sheets.

Trạng thái hiện tại: **tính năng Phase 1–3 đã triển khai; host tests,
Apps Script tests và ESP-IDF build đều PASS**. Luồng điểm danh sơ bộ với hai
học sinh đã chạy ổn; nghiệm thu cuối trên board (âm thanh, lỗi mạng, OTA và
30 phút ổn định) vẫn cần biên bản trước khi gắn nhãn release. Bắt đầu từ
[FULL_SYSTEM_ACCEPTANCE_TEST.md](FULL_SYSTEM_ACCEPTANCE_TEST.md).

## Luồng hệ thống

Camera trên board → ESP-DL nhận diện tại thiết bị → ghi sự kiện vào flash và
outbox → gửi HTTPS tới Apps Script → Google Sheets lưu event duy nhất theo
`event_id` → dashboard giáo viên hiển thị session, trạng thái điểm danh và
device health. DFPlayer phát phản hồi qua queue UART riêng; hỏng loa/thẻ không
được chặn luồng điểm danh. OTA dùng hai app slot và chỉ xác nhận image mới sau
health gate.

## Phase 3 — âm thanh

DFPlayer Mini dùng UART1 9600 8N1: TX GPIO41 qua điện trở 1 kΩ tới DFPlayer RX,
RX GPIO42 nhận từ DFPlayer TX; module dùng 5V và GND chung. Loa nối trực tiếp
giữa SPK1/SPK2. Sáu file nằm trong thư mục /mp3 trên microSD; volume, mute và
test track có tại maintenance portal. Audio chạy qua queue riêng và luôn là
ngoại vi optional: lỗi audio không được chặn attendance hay kích hoạt OTA
rollback.

## Cấu hình runtime

Từ P2.3, firmware build được mà không cần header chứa secret. Wi-Fi, Apps
Script `/exec` URL, device ID và device secret được nhập tại maintenance portal
và lưu trong NVS theo hai slot A/B có schema version, generation và CRC32.

- Lần đầu/chưa có cấu hình: AP `RobotSetup`, mật khẩu `RobotSetup-2026`.
- Thiết bị đã cấu hình: mật khẩu maintenance AP là `Cfg-` cộng 12 ký tự đầu
  của device secret.
- Portal: `http://192.168.4.1/config`.

Cấu hình mới chỉ được kích hoạt sau khi Wi-Fi, NTP và heartbeat xác thực với
Apps Script đều thành công. Cấu hình cũ được giữ để rollback. Hai file local
legacy `wifi_time_config.h` và `cloud_config.h` vẫn bị Git ignore nhưng không
còn được firmware include.

Maintenance portal có thể được mở lại bằng cách giữ BOOT 2,5 giây **khi firmware
đang chạy**. Board chốt yêu cầu maintenance vào NVS,
tự khởi động lại rồi mở portal. Cờ này chỉ được xóa khi người dùng chủ động rời
maintenance, vì vậy reset bất ngờ không làm AP biến mất. Không giữ BOOT trong
lúc nhấn RST vì GPIO0 khi đó chọn ROM download mode. Mật khẩu AP không được in
ra log.

## Backend Google Sheets

Xem [google_apps_script/README.md](google_apps_script/README.md). Backend dùng
`event_id` để nhận retry an toàn; cùng một event chỉ tạo tối đa một dòng. API
v2 hỗ trợ attendance, heartbeat định kỳ, session sync, OTA manifest có xác
thực và secret riêng theo thiết bị. Dashboard tính ONLINE/OFFLINE từ Last Seen
và hiển thị RSSI, heap, PSRAM, pending events, sync latency cùng lỗi gần nhất;
payload Phase 1 vẫn được
chấp nhận khi migration mode còn bật.

## Dữ liệu trên thiết bị

- NVS namespace `device_cfg`: runtime config A/B có schema, generation và CRC32.
- NVS namespace `student_db`: tên và lớp theo face ID.
- `/spiffs/attendance.csv`: lịch sử điểm danh.
- `/spiffs/upload_outbox.bin`: các event đang chờ server xác nhận.
- Face embeddings: database ESP-DL trong partition SPIFFS `faces`.

Outbox là nguồn dữ liệu upload chuẩn: record được ghi xuống flash trước khi gửi
và chỉ bị xóa khi server trả về `{"ok":true}`. Từ P2.4, record v2 có CRC32;
firmware vẫn đọc và tự nâng outbox v1 hiện có, đồng thời bỏ record bị cắt
cụt/checksum sai mà không chặn các record hợp lệ phía sau.

## Build

Dự án dùng ESP-IDF 6.0.2, target `esp32s3`:

```bash
cd firmware
idf.py build
```

Binary đầu ra: `firmware/build/robot_attendance.bin`. P2.5 dùng `ota_0` và
`ota_1` 6 MiB, giữ nguyên offset NVS/`faces`; CI và
`tools/package_ota.sh` tạo thêm SHA-256 cùng manifest release. Không dùng
`erase-flash` khi cập nhật firmware thông thường vì thao tác đó xóa cấu hình,
danh tính và outbox.

## Kiểm thử và dữ liệu riêng tư

Từ thư mục gốc của repo, với GCC, Node.js và ESP-IDF 6.0.2 đã cài, chạy:

```bash
bash tests/host/run_tests.sh
node tests/apps_script/test_code.js
node tests/apps_script/test_dashboard_html.js
cd firmware && eim run "idf.py build" v6.0.2
```

Nếu `node` không có trong `PATH`, đặt `NODE_BIN` tới executable Node.js khi
chạy host tests và dùng executable đó cho hai test Apps Script.

Ảnh mặt, embeddings, danh sách học sinh, log serial, file CSV xuất ra và
credential triển khai là dữ liệu riêng tư: không commit chúng. Git ignore
file cấu hình local, build artefacts và thư mục `test-evidence/`. AP bootstrap
có mật khẩu công khai để cấu hình lần đầu; chỉ bật trên bench tin cậy và
provision device secret ngay sau khi flash.

Nhận diện đúng phần cứng, kiểm tra cáp camera, cài ESP-IDF và quy trình
flash/monitor trên Ubuntu nằm trong
[HARDWARE_AND_UBUNTU_GUIDE.md](HARDWARE_AND_UBUNTU_GUIDE.md).

## Tài liệu theo phase

- Phase 1: [release notes](PHASE1_RELEASE_NOTES.md),
  [test guide](PHASE1_TEST_GUIDE.md).
- Phase 2: [kế hoạch](PHASE2_PLAN.md), [tiến độ](PHASE2_PROGRESS.md),
  [API v2](P2_1_TEST_GUIDE.md), [dashboard](P2_2_TEST_GUIDE.md),
  [full flow P2.2/P2.3](P2_2_P2_3_FULL_FLOW_TEST_GUIDE.md),
  [device health](P2_4_TEST_GUIDE.md), [OTA/rollback](P2_5_TEST_GUIDE.md).
- Phase 3: [kế hoạch](PHASE3_PLAN.md), [tiến độ](PHASE3_PROGRESS.md),
  [DFPlayer và đấu dây](P3_TEST_GUIDE.md).
- Backend: [thiết lập Apps Script](google_apps_script/README.md).
