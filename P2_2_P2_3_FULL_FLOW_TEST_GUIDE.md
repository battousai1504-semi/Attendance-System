# Nghiệm thu full flow P2.2 + P2.3

> Tài liệu nghiệm thu lịch sử cho Phase 2. Firmware hiện tại thuộc Phase 3;
> dùng `FULL_SYSTEM_ACCEPTANCE_TEST.md` để test toàn hệ thống.

Tài liệu này kiểm tra một lượt từ Google Sheets/dashboard đến runtime
provisioning, nhận diện trên ESP32-S3 và dữ liệu điểm danh. Không dùng
`erase-flash`: bài test phải chứng minh firmware update vẫn giữ khuôn mặt,
lịch sử local và outbox.

## 1. Phần cứng và kết nối

Board đang dùng là ESP32-S3-WROOM N16R8 camera tích hợp. Camera đi qua cáp FPC
nội bộ, không nối lại các GPIO camera ra breadboard. Trong P2.2/P2.3 chưa có
DFPlayer hay loa; kết nối vật lý duy nhất để flash/test là:

```text
Ubuntu laptop ── cáp USB data ── cổng USB-UART của board
Camera OV2640 ── cáp FPC nguyên bản ── socket camera trên board
```

Không cắm nhầm cổng USB native/OTG khi đang tìm `/dev/ttyACM0`. Kiểm tra port:

```bash
ls -l /dev/serial/by-id/ 2>/dev/null
```

Ưu tiên symlink ổn định trong `/dev/serial/by-id/` của **chính board đang
cắm**; tên sẽ khác nhau giữa các máy và thiết bị.

## 2. Chuẩn bị Apps Script P2.2/P2.3

1. Dán bản mới nhất của `google_apps_script/Code.gs` vào Apps Script.
2. Tạo HTML file tên chính xác `Dashboard`, dán `Dashboard.html`.
3. Chạy `setup`, rồi `migrateToV2`.
4. Trong Script Properties, tạo:
   - `BOOTSTRAP_DEVICE_ID=robot-01`;
   - `BOOTSTRAP_DEVICE_SECRET=<secret 32–127 ký tự>`;
   - `BOOTSTRAP_DASHBOARD_KEY=<dashboard key 12–127 ký tự>`.
5. Chạy `provisionDeviceFromProperties`.
6. Chạy `provisionDashboardAccessFromProperties`.
7. Xác nhận hai property plaintext đã bị xóa; sheet `Devices` chỉ chứa hash.
8. Deploy loại **Ứng dụng web**, execute as **Tôi**, access **Bất kỳ ai**, tạo
   version mới.

Kiểm tra hai URL:

```text
<EXEC_URL>
<EXEC_URL>?view=dashboard
```

URL đầu phải trả JSON health có `ok:true`, `api_version:2` và
`schema_version:"2"`. URL thứ hai phải mở form đăng nhập dashboard.

Ghi tạm ba giá trị để nhập vào portal, không đưa chúng vào Git hoặc ảnh chụp:

```text
EXEC_URL
DEVICE_ID=robot-01
DEVICE_SECRET
```

## 3. Chuẩn bị roster P2.2

Trong sheet `Students`, tạo hai học sinh khớp chính xác ID đã enroll trên
board:

| Student ID | Name | Class | Active | Created At | Updated At |
|---:|---|---|---|---|---|
| 1 | Học sinh A | 12A1 | TRUE | thời gian hiện tại | thời gian hiện tại |
| 2 | Học sinh B | 12A1 | TRUE | thời gian hiện tại | thời gian hiện tại |

Tên và lớp trong `Students` phải khớp metadata trên board. Nếu board dùng lớp
khác, thay toàn bộ `12A1` trong bài test bằng đúng lớp đó.

## 4. Build và flash P2.3 trên Ubuntu

Mở ESP-IDF shell:

```bash
eim shell v6.0.2
cd "$(git rev-parse --show-toplevel)/firmware"
idf.py --version
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Thay `/dev/ttyACM0` bằng symlink đã xác minh nếu khác. Không chạy
`erase-flash`. Phiên bản mong đợi:

```text
Robot Điểm Danh 2.4.0-phase2-dev
```

Firmware P2.3 không cần `wifi_time_config.h` hoặc `cloud_config.h`. Nếu bước
đóng gói model báo `python: not found`, kích hoạt đúng EIM shell; phương án chẩn
đoán tạm trên máy hiện tại là đưa virtualenv ESP-IDF vào PATH trước khi build:

```bash
export PATH="$HOME/.espressif/tools/python/v6.0.2/venv/bin:$PATH"
```

## 5. Provision cấu hình runtime lần đầu

Do firmware cũ chưa có namespace P2.3, boot đầu sẽ báo không có runtime config
và mở portal:

```text
No valid runtime network/cloud configuration; maintenance portal required
Maintenance portal active at http://192.168.4.1 on SSID 'RobotSetup'
```

Trên điện thoại/laptop:

1. Kết nối Wi-Fi `RobotSetup`.
2. Mật khẩu lần đầu: `RobotSetup-2026`.
3. Tắt mobile data/VPN nếu trình duyệt tự bỏ mạng “không có Internet”.
4. Mở `http://192.168.4.1/config`.
5. Bấm **Quét Wi-Fi 2,4 GHz**, chọn Wi-Fi thật.
6. Nhập mật khẩu Wi-Fi, `EXEC_URL`, `robot-01` và `DEVICE_SECRET`.
7. Bấm **Kiểm tra và lưu cấu hình**.
8. AP có thể ngắt vài giây khi radio chuyển sang kênh Wi-Fi thật. Giữ trang mở
   hoặc kết nối lại `RobotSetup`; giao diện sẽ tiếp tục đọc trạng thái job.
9. Chỉ khi trang báo đã lưu generation/slot, bấm **Khởi động lại để áp dụng**.

Portal phải thực hiện theo thứ tự: kết nối Wi-Fi → đồng bộ NTP → gửi heartbeat
xác thực → ghi slot inactive → kiểm CRC → đổi active slot. Việc restart chỉ
được thực hiện sau khi trình duyệt đã nhận kết quả thành công. Serial
không được in Wi-Fi password, URL đầy đủ hoặc device secret.

Sau restart, log mong đợi:

```text
Loaded runtime config schema=1 generation=1 slot=A
Connecting to configured Wi-Fi network
NTP time synchronized
Durable uploader ready
=== Bắt đầu vòng lặp điểm danh ===
```

Slot đầu có thể là A; từ lần lưu tiếp theo phải luân phiên A/B. Sheet `Devices`
phải cập nhật `Last Seen` từ heartbeat probe.

## 6. Test chống mất cấu hình khi nhập sai

1. Khi robot đang nhận diện, giữ BOOT khoảng 2,5 giây, không nhấn RST.
2. Chờ robot restart và mở `RobotSetup`.
3. Với thiết bị đã cấu hình, mật khẩu AP là `Cfg-` cộng 12 ký tự đầu của
   `DEVICE_SECRET`.
4. Mở `/config`, nhập sai mật khẩu Wi-Fi nhưng giữ các trường khác đúng.
5. Bấm **Kiểm tra và lưu cấu hình**.

Kết quả bắt buộc:

- portal trả `wifi_connection_failed`;
- robot không báo `Activated runtime config`;
- generation/active slot không đổi;
- nhấn RST rồi boot lại vẫn kết nối bằng cấu hình cũ.

## 7. Test session và điểm danh P2.2

Mở dashboard bằng laptop, đăng nhập bằng dashboard key và tên người thao tác.

### Buổi 1: Present + Absent/Excused

1. Tạo session ID duy nhất, ví dụ `12A1-20260922-P1`.
2. Class `12A1`, device `robot-01`.
3. `Starts At`: trước hiện tại 5 phút.
4. `Late After`: sau hiện tại 5 phút.
5. `Ends At`: sau hiện tại 30 phút.
6. Cho học sinh ID 1 nhìn camera; không cho ID 2 xuất hiện.
7. Chờ log `Uploaded event=...`, rồi refresh dashboard.

Kết quả:

```text
Sĩ số 2 | Có mặt 1 | Đi muộn 0 | Vắng 1 | Có phép 0
```

Đổi ID 2 từ `Vắng` sang `Có phép`, nhập lý do. Kiểm tra:

- tổng đổi thành `Có phép 1`, `Vắng 0`;
- `Attendance` không bị sửa hoặc giảm số dòng;
- `AuditLog` có `ATTENDANCE_OVERRIDE`, actor, before/after và reason;
- reload trang vẫn giữ trạng thái override.

Xuất CSV và mở bằng LibreOffice; tiếng Việt, Event ID và status phải đúng.
Sau đó đóng session và xác nhận `SESSION_STATUS_CHANGE` trong `AuditLog`.

### Buổi 2: Late

1. Tạo session `12A1-20260922-P2` sau khi buổi 1 đã đóng.
2. `Starts At`: trước hiện tại 5 phút.
3. `Late After`: trước hiện tại 1 phút.
4. `Ends At`: sau hiện tại 30 phút.
5. Cho học sinh ID 2 nhìn camera.

Dashboard phải đánh dấu ID 2 là `Đi muộn`. Kiểm tra bộ lọc lớp, ngày, session,
tìm theo tên/ID và hiển thị trên điện thoại. Với mọi session:

```text
present + late + absent + excused = total
```

## 8. Test reboot persistence, slot A/B và rollback

1. Nhấn RST, không giữ BOOT.
2. Xác nhận generation và active slot vẫn giống trước reboot.
3. Giữ BOOT 2,5 giây để mở portal.
4. Lưu lại cùng một cấu hình hợp lệ. Đây là cách an toàn tạo generation tiếp
   theo mà không cần một Wi-Fi thứ hai.
5. Sau restart, log phải cho thấy generation tăng 1 và slot đổi A ↔ B.
6. Mở maintenance lần nữa, bấm **Rollback cấu hình trước**, nhập `ROLLBACK`.
7. Sau restart, log phải quay về generation trước và robot vẫn upload được.

Rollback không xóa slot mới; lần update hợp lệ tiếp theo sẽ ghi đè slot
inactive theo transaction mới.

## 9. Test factory reset có phạm vi

Thực hiện cuối cùng vì nó buộc phải provision lại:

1. Mở maintenance portal.
2. Bấm **Factory reset cấu hình mạng/cloud**.
3. Nhập chính xác `RESET CONFIG`.
4. Robot restart và mở `RobotSetup` với mật khẩu bootstrap
   `RobotSetup-2026`.
5. Kiểm tra danh sách học sinh/khuôn mặt vẫn còn nguyên.
6. Provision lại cấu hình đúng theo mục 5.
7. Điểm danh một học sinh và xác nhận upload thành công.

Reset này chỉ xóa namespace mạng/cloud; không xóa embeddings, `student_db`,
attendance CSV hoặc outbox. Nút **Xóa toàn bộ danh tính** là thao tác riêng và
không được dùng trong bài nghiệm thu này.

## 10. Xử lý thông báo `Failed to fetch`

ESP32-S3 dùng chung một radio cho AP cấu hình và kết nối Wi-Fi thật. Khi thử
SSID, AP có thể đổi kênh và ngắt trình duyệt trong vài giây; đây chưa phải là
kết luận lưu thất bại.

1. Không bấm lưu lặp lại và không reset board.
2. Kết nối lại `RobotSetup`, rồi mở lại `http://192.168.4.1/config`.
3. Firmware mới sẽ tiếp tục đọc `/config/job` và hiển thị một trong ba trạng
   thái: đang kiểm tra, đã lưu, hoặc lỗi cụ thể.
4. Chỉ bấm **Khởi động lại để áp dụng** khi trang báo generation/slot đã lưu.

Nếu firmware cũ đã in cả `Provisioning cloud authentication probe accepted`
và `Activated runtime config` thì cấu hình thực tế đã được lưu dù trình duyệt
báo `Failed to fetch`. Flash bản mới (không `erase-flash`) để giữ dữ liệu và
loại bỏ race này.

### Nếu Wi-Fi `RobotSetup` tự biến mất khi chưa bấm lưu

Firmware mới giữ cờ maintenance qua reset bất ngờ và tắt modem power-save khi
portal hoạt động. Theo dõi serial để phân loại:

- xuất hiện lại banner `Robot Điểm Danh` cùng `Reset reason`: board đã reset;
  `brownout` thường là nguồn/cáp, còn `panic` hoặc `watchdog` cần giữ toàn bộ
  backtrace để sửa firmware;
- chỉ có `Maintenance client disconnected` mà không có banner boot mới: board
  vẫn chạy, Ubuntu/điện thoại đã tự rời AP không có Internet;
- `Maintenance client connected/disconnected` kèm `reason` giúp xác định phía
  client hay phía AP chủ động ngắt.

Trên Ubuntu có thể giữ profile nội bộ mà không dùng nó làm đường Internet:

```bash
nmcli connection modify RobotSetup connection.autoconnect yes \
  ipv4.never-default yes ipv6.never-default yes
nmcli connection up RobotSetup
```

Nếu tên profile khác SSID, xem bằng `nmcli connection show` rồi thay
`RobotSetup` bằng đúng tên profile. Trên điện thoại, tắt mobile data/VPN và
chọn giữ kết nối khi máy cảnh báo mạng không có Internet.


## 11. Test camera trong lúc uploader retry

1. Điểm danh một học sinh và giữ khuôn mặt ngoài khung hình sau khi robot ghi
   nhận.
2. Giữ monitor ít nhất hai phút để uploader hoàn thành hoặc retry.
3. Đưa một học sinh khác vào khung hình mà không restart board.
4. Camera phải tiếp tục nhận diện và tạo event mới.

Một frame lỗi đơn lẻ được xử lý như sau và không làm dừng camera:

```text
Dropping invalid JPEG frame ... attempt=1/3
```

Nếu ba lần lấy mới đều hỏng, log tăng bộ đếm pipeline. Sau ba chu kỳ liên tiếp,
firmware tự chạy `esp_camera_deinit/init` và phải báo:

```text
Reinitializing camera after repeated frame failures
Camera recovery succeeded
```

Log `status=404 ... json=0` rồi một dòng `Uploaded event=...` sau đó nghĩa là
Google redirect tạm lỗi nhưng outbox retry đã thành công; không tạo thêm dòng
Attendance và không cần reset camera.

## 12. Tiêu chí đóng P2.2 và P2.3

- dashboard chạy được trên laptop và điện thoại;
- hai session cho kết quả Present/Late/Absent/Excused đúng;
- correction chỉ ghi AuditLog, không sửa event gốc;
- firmware build/flash không cần header secret;
- cấu hình sống qua reboot;
- Wi-Fi/secret sai không thay active config;
- save hợp lệ luân phiên A/B và rollback được;
- factory reset config giữ nguyên danh tính và lịch sử;
- serial log/HTTP response không lộ password hoặc secret;
- điểm danh sau lần provision cuối xuất hiện đúng một lần trên Sheet;
- camera tiếp tục nhận diện sau khi uploader redirect/retry.

Khi toàn bộ mục trên pass, P2.2 và P2.3 đủ điều kiện đóng. Bước tiếp theo là
P2.4: heartbeat định kỳ, trạng thái online/offline, telemetry và fault
injection cho outbox.
