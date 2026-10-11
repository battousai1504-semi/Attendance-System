# Robot Attendance

Firmware điểm danh khuôn mặt cho board ESP32-S3-WROOM N16R8 camera tích hợp
(pinout kiểu Freenove/ESP32S3_EYE). Thiết bị đăng ký
khuôn mặt qua Wi-Fi AP, nhận diện tại edge, lưu lịch sử cục bộ và đồng bộ bền
vững lên Google Sheets.

Trạng thái hiện tại: **tính năng Phase 1–3 đã triển khai; host tests,
Apps Script tests và ESP-IDF build đều PASS**. Luồng điểm danh với hai học sinh
đã chạy ổn. Bản nâng cấp `3.1.0-phase3-ux-dev` thêm lời chào theo Student ID
và dashboard giáo viên gọn hơn. Người dùng đã xác nhận phần loa Phase 3 PASS
trên thiết bị; dashboard UX mới vẫn cần test trên Web App trước khi gắn nhãn
release. OTA/rollback vẫn chưa được nghiệm
thu vì chưa có HTTPS host cho binary. Bắt đầu từ
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
giữa SPK1/SPK2. Sáu câu chung và một câu trọn vẹn cho mỗi Student ID nằm trong
`/mp3` trên microSD (`1001.mp3` ↔ ID 1, …, `1050.mp3` ↔ ID 50). DFPlayer
không tổng hợp giọng nói từ tên; file lời chào phải được chuẩn bị khi enroll,
theo [P3_TEST_GUIDE.md](P3_TEST_GUIDE.md). Âm lượng mặc định 30/30 đã kiểm thử;
bật/tắt và test track có tại
maintenance portal. Audio chạy qua queue riêng và luôn là
ngoại vi optional: lỗi audio không được chặn attendance hay kích hoạt OTA
rollback.

Danh sách lớp Excel có thể chuyển thành bảng câu đọc theo **Student ID thật
trên robot** bằng [hướng dẫn ghi lời chào](AUDIO_CLASS_ROSTER_GUIDE.md).

## Luồng giáo viên hằng ngày

Đăng nhập dashboard bằng tài khoản giáo viên → nếu buổi đang hoạt động, xem ngay lớp/buổi và
máy điểm danh đang phục vụ. Nếu chưa có, nút một chạm chỉ gợi ý **đúng buổi
Sáng hoặc Chiều hiện tại** từ lớp đã học gần đây, khi máy đang online. Với lớp
khác, chọn lớp đang được cấu hình và buổi học rồi bấm **Bắt đầu điểm danh**.
Mặc định buổi Sáng 07:00–10:30, Chiều 14:00–16:30 (giờ Việt Nam), đi muộn
sau 10 phút. Hệ thống tự đặt mã buổi; chỉ mở **Tùy chọn nâng cao** khi nhà
trường đổi giờ. Trong giờ học, xem tổng số và danh sách điểm danh, tìm tên,
chỉnh trường hợp ngoại lệ kèm lý do. Cuối giờ
buổi tự đóng theo trigger hoặc giáo viên bấm **Đóng buổi học**, sau đó **Xuất
CSV**. Lọc buổi cũ và thông số thiết bị vẫn có nhưng được thu gọn khỏi màn
hình chính. Dashboard cảnh báo lượt upload hôm nay chưa được gắn vào buổi học
nào; lượt đó không có trong sĩ số. Cấu hình Wi-Fi, enroll và chuẩn bị câu nói tên là việc setup một
lần của người quản trị, không phải thao tác hằng ngày của giáo viên.

Nút gợi ý lấy buổi gần nhất **cùng Sáng/Chiều**, chưa phải thời khóa biểu lưu cố định.
Robot hiện vẫn phát câu thành công sau khi lưu cục bộ; việc đối chiếu buổi học
chỉ diễn ra ở backend khi upload. Không diễn giải âm thanh này là xác nhận từ
Sheet trong lúc chưa có cơ chế session-aware trên thiết bị.

## Cấu hình runtime

Từ P2.3, firmware build được mà không cần header chứa secret. Wi-Fi, Apps
Script `/exec` URL, device ID và device secret được nhập tại maintenance portal
và lưu trong NVS theo hai slot A/B có schema version, generation và CRC32.

- AP `RobotSetup` dùng cùng mật khẩu bàn giao ở cả lần đầu và khi mở maintenance.
  Bản build trên máy này đọc mật khẩu từ `firmware/main/setup_ap_password.h`
  (Git ignore). Bản build công khai không có file đó sẽ dùng mật khẩu phát triển
  `RobotSetup-2026`; **không bàn giao bản build công khai nguyên trạng**. Ngược
  lại, không tải binary bàn giao có chứa mật khẩu riêng lên GitHub Release.
- Portal: `http://192.168.4.1/config`.

Cấu hình mới chỉ được kích hoạt sau khi Wi-Fi, NTP và heartbeat xác thực với
Apps Script đều thành công. Cấu hình cũ được giữ để rollback. Hai file local
legacy `wifi_time_config.h` và `cloud_config.h` vẫn bị Git ignore nhưng không
còn được firmware include.

Trang `/config` nay ưu tiên đổi Wi-Fi. Khi đổi cùng SSID, để trống mật khẩu
để giữ mật khẩu cũ. URL Apps Script, Device ID và device secret nằm trong mục
**Cài đặt kỹ thuật**; để trống giữ giá trị NVS hiện tại. Cài đặt lần đầu hoặc
xoay khóa mới vẫn cần nhập đủ. Device secret không được gửi ngược về trình
duyệt; nó là khóa để robot xác thực với backend, khác mật khẩu đăng nhập giáo
viên và mật khẩu AP.

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
file cấu hình local, build artefacts và thư mục `test-evidence/`. AP của bản
bàn giao dùng mật khẩu cục bộ không đưa lên Git; bản build công khai vẫn có
mật khẩu phát triển nên chỉ bật trên bench tin cậy và provision device secret
ngay sau khi flash.

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
- UX giáo viên: [kiểm thử bản một chạm](ENDUSER_UX_TEST_GUIDE.md).
- OTA hiện tại: [bộ image và quy trình thử rollback 11/10/2026](OTA_ROLLBACK_E2E_2026-10-11.md)
  (đã phát hành pre-release thử nghiệm, chưa nghiệm thu trên board).
