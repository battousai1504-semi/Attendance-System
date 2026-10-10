# Nghiệm thu P2.4 — Device health và độ bền đồng bộ

> Tài liệu nghiệm thu lịch sử cho mốc P2.4. Để kiểm thử firmware hiện tại,
> dùng `FULL_SYSTEM_ACCEPTANCE_TEST.md`; version `2.4.0-phase2-dev` bên dưới
> chỉ là kỳ vọng tại thời điểm Phase 2.

Ngày cập nhật: 2026-09-25

Mục tiêu của lượt test này là xác nhận heartbeat không chặn camera, dashboard
phản ánh đúng online/offline, event offline sống qua reboot, backoff không spam
server và outbox v1 được nâng lên v2 có CRC32 mà không cần xóa flash.

## 1. Cập nhật Apps Script

Trong project Apps Script đang dùng:

1. Thay toàn bộ `Code.gs` và `Dashboard.html` bằng bản mới trong repository.
2. Chạy `migrateToV2`.
3. Kiểm tra sheet `Devices` có 16 cột; ba cột cuối là `Status`,
   `Sync Latency (ms)`, `Consecutive Failures`.
4. Chạy `installPhase2Automation`.
5. Trong **Triggers**, xác nhận đúng:
   - `closeExpiredSessions`: mỗi 5 phút;
   - `refreshDeviceStatuses`: mỗi 1 phút.
6. Cập nhật Web App deployment sang version mới. Execute as owner và access
   vẫn là **Anyone**.

Không tạo deployment URL mới nếu không cần. Nếu URL thay đổi thì phải cập nhật
lại runtime config của board qua maintenance portal.

## 2. Build và flash trên Ubuntu

Không chạy `erase-flash`; P2.4 phải giữ cấu hình, face database, lịch sử và
outbox cũ.

```bash
cd "$(git rev-parse --show-toplevel)/firmware"
eim shell v6.0.2
idf.py --version
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Thay `/dev/ttyACM0` bằng symlink thật của board trong `/dev/serial/by-id/`
nếu khác.

Nếu không muốn mở interactive shell:

```bash
eim run "idf.py build" v6.0.2
```

Phiên bản mong đợi:

```text
Robot Điểm Danh 2.4.0-phase2-dev
P2.4 uploader ready: pending=... heartbeat=60s outbox=v2
```

Nếu máy không có đúng symlink serial, kiểm tra bằng:

```bash
ls -l /dev/serial/by-id/
```

## 3. Heartbeat và telemetry

Giữ board online ít nhất ba phút. Khoảng mỗi 60 giây phải có:

```text
Heartbeat accepted: rssi=... heap=... psram=... pending=... latency=...ms
```

Trong sheet `Devices` và dashboard, kiểm tra:

- `Last Seen` thay đổi;
- `Status = ONLINE`;
- firmware là `2.4.0-phase2-dev`;
- RSSI, free heap, free PSRAM, uptime và sync latency có giá trị hợp lệ;
- pending events về 0 sau khi đồng bộ;
- camera vẫn nhận diện trong lúc heartbeat HTTPS đang chạy.

## 4. Test offline/online

1. Khi board đang ONLINE, tắt router hoặc chặn Internet của mạng 2,4 GHz.
2. Giữ board chạy và theo dõi serial.
3. Sau 180 giây không heartbeat, trigger một phút sẽ chuyển thiết bị sang
   `OFFLINE`. Do lịch trigger, độ trễ quan sát tối đa khoảng bốn phút.
4. Bật mạng lại.
5. Board phải tự kết nối, heartbeat tiếp theo chuyển trạng thái về `ONLINE`.

Khi cloud lỗi, serial phải tăng backoff thay vì retry dồn dập:

```text
Cloud backoff: failures=1 next_retry=...
Cloud backoff: failures=2 next_retry=...
```

Delay tăng theo cấp số nhân từ 5 giây, có jitter ±25% và bị chặn tối đa ở
300 giây.

## 5. Test durable outbox qua reboot

1. Tắt Internet nhưng giữ Wi-Fi/router nếu có thể để clock của board vẫn hợp lệ.
2. Điểm danh một học sinh và lưu lại Event ID từ serial.
3. Xác nhận `pending_events` tăng và event chưa có trên Sheet.
4. Nhấn RST hoặc rút/cắm USB một lần. Không erase flash.
5. Khôi phục Internet.
6. Chờ dòng `Uploaded event=<Event ID>`.
7. Trên `Attendance`, Event ID đó phải xuất hiện đúng một lần.

Nếu board reboot khi chưa có thời gian hợp lệ, firmware sẽ chờ NTP trước khi
cho tạo event mới; event đã nằm trong outbox vẫn được giữ nguyên.

## 6. Test lỗi và phục hồi camera đồng thời

Trong lúc uploader retry hoặc heartbeat chạy:

1. Điểm danh học sinh A.
2. Chờ ít nhất hai chu kỳ heartbeat.
3. Không reset board, đưa học sinh B vào khung hình.
4. Cả hai phải được nhận diện; không được có camera freeze.

Một JPEG lỗi đơn lẻ được bỏ và lấy lại. Nếu pipeline lỗi lặp lại, firmware tự
khởi tạo lại camera theo cơ chế P2.3.

## 7. Tiêu chí đóng P2.4

P2.4 chỉ được đóng sau khi có đủ bằng chứng:

- Apps Script tests, host CRC/backoff tests và ESP-IDF build đều PASS;
- board gửi ít nhất ba heartbeat liên tiếp;
- dashboard chuyển ONLINE → OFFLINE → ONLINE đúng;
- telemetry không chứa Wi-Fi password, device secret hay deployment URL;
- event offline sống qua reboot và lên Sheet đúng một lần;
- nhận diện vẫn hoạt động trong heartbeat/upload retry;
- không panic, watchdog, brownout hoặc reset loop.

Sau khi đạt đủ, lưu serial log đã che secret, ảnh sheet `Devices`, ảnh
dashboard và Event ID đối chiếu vào test report. Milestone tiếp theo là P2.5:
OTA hai partition và rollback.
