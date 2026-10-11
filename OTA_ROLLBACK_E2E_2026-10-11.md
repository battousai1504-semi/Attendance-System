# OTA/rollback E2E — bộ thử nghiệm 11/10/2026

**Trạng thái: pre-release đã lên GitHub và URL/sha256 đã kiểm tra; chưa thử OTA
trên robot.** Không đánh dấu PASS cho đến khi có log từ board thật và xác nhận
dữ liệu/điểm danh sau cập nhật. Hai file dưới `firmware/build-ota-ux-e2e/packages/`
là artifact local bị Git bỏ qua; bản `3.1.2-rollback-e2e` **cố ý lỗi** và tuyệt
đối không flash qua USB hay dùng làm bản chạy chính thức.

| Image | Kích thước | SHA-256 |
|---|---:|---|
| `robot_attendance-3.1.1-ota-e2e.bin` | 4.728.992 byte | `d5f660325ca5cc69268eca3eb8d49190db6d56df769595a681d07c038356e0dc` |
| `robot_attendance-3.1.2-rollback-e2e.bin` | 3.985.248 byte | `cc7f5f5fa662cca8fbb97f88f269dbbdbf15ffbb55b61723b437fff39854270d` |

Hai image build bằng ESP-IDF v6.0.2 từ cùng source của nhánh
`feature/personalized-audio-teacher-ux` tại commit `634474a`.
`esptool image-info` xác nhận đúng ESP32-S3, app version và image checksum; cả
hai nhỏ hơn OTA slot 6 MiB. [Pre-release công khai](https://github.com/battousai1504-semi/Attendance-System/releases/tag/ota-e2e-20261011)
có đúng hai asset. Cả hai URL trong `*.manifest.json` đã tải lại được **không
cần đăng nhập**, với kích thước và SHA-256 trùng bảng trên.

## Trước khi bật OTA

1. Xác nhận qua serial robot đang chạy firmware OTA-capable, có hai slot
   `ota_0`/`ota_1` và hoạt động tốt. Ghi lại version, số học sinh, số khuôn mặt,
   pending events. Nếu firmware đang chạy không phải bản đã test, dừng và đối
   chiếu partition table trước khi cập nhật. Trên Ubuntu:

   ```bash
   ls -l /dev/serial/by-id/
   cd firmware
   eim run "idf.py -p /dev/ttyACM0 monitor" v6.0.2
   ```

   Thay `/dev/ttyACM0` bằng cổng thực tế theo symlink. Chỉ dùng `monitor`,
   **không flash image test qua USB** và không dùng `erase-flash`.
2. Giữ `OTA_ENABLED=false` trong Apps Script. Sao lưu Sheet và dữ liệu cần giữ.
   Test khi không có buổi học đang ACTIVE, cấp nguồn ổn định và có cáp serial
   để đọc log. Không thử lần đầu trên máy đang phục vụ lớp.
3. Binary build hiện tại nhúng mật khẩu AP `RobotSetup` của bản bàn giao; chủ
   dự án đã chấp nhận công bố hai image này. Không đăng device secret, Wi-Fi
   nhà trường, ảnh mặt hoặc sheet học sinh lên release. Về lâu dài cần thay
   mật khẩu AP này bằng một bí mật riêng cho từng máy.
4. URL đã được kiểm tra từ máy build. Nếu cần xác nhận lại từ mạng của bạn,
   dùng `curl -fL -o <file> <URL>` với URL trong manifest rồi so `sha256sum`
   và `stat -c %s` với bảng trên. Không dùng URL preview/trang HTML.

## Cập nhật bản tốt

1. Trong Script Properties, đặt `OTA_VERSION`, `OTA_URL`, `OTA_SIZE`,
   `OTA_SHA256` theo manifest `3.1.1-ota-e2e.manifest.json`; cuối cùng mới đặt
   `OTA_ENABLED=true`. Không cần
   redeploy Web App khi chỉ đổi Script Properties.
2. Nhấn RST hoặc chờ lần kiểm tra định kỳ. Sau boot, OTA check đầu tiên khoảng
   60 giây; các lần sau cách khoảng 6 giờ nếu không có update. Quan sát:
   `Downloading OTA 3.1.1-ota-e2e` → `verified and selected; restarting` →
   `Robot Điểm Danh 3.1.1-ota-e2e` → `OTA health gate passed; running image
   marked VALID`. Nếu không có những marker này, không đánh dấu PASS.
3. Đặt lại `OTA_ENABLED=false`. Khởi động lại lần nữa: vẫn là `3.1.1-ota-e2e`.
   Kiểm tra camera, lời chào đúng tên, local attendance, upload và dashboard
   firmware version. Dữ liệu học sinh, embeddings, lịch sử và outbox phải còn.

## Từ chối SHA sai và mất nguồn giữa tải

- Với baseline tốt đang chạy, tạm thay `OTA_SHA256` của **image khác version**
  bằng chuỗi 64 hex sai, bật OTA. Sau khi tải xong phải thấy `OTA SHA-256
  verification failed; boot partition unchanged`. Tắt OTA, reset, xác nhận
  version và dữ liệu không đổi. Không dùng chính version đang chạy: server sẽ
  trả `available:false`.
- Lặp lại với SHA đúng. Khi log tải 10–90%, ngắt nguồn có kiểm soát. **Tắt
  `OTA_ENABLED` trước khi cấp nguồn lại**, rồi kiểm tra boot về bản tốt, không
  mất dữ liệu. Không cắt nguồn khi đang ghi NVS/đăng ký khuôn mặt.

## Chứng minh automatic rollback

1. Chỉ chạy sau khi `3.1.1-ota-e2e` đã qua health gate và reboot vẫn tốt. Đặt
   bốn property theo `3.1.2-rollback-e2e.manifest.json`, sau đó bật
   `OTA_ENABLED=true`.
2. Ngay khi serial báo `Downloading OTA 3.1.2-rollback-e2e`, đặt
   `OTA_ENABLED=false` để bản tốt **không tải lại image lỗi** sau rollback.
   Thay đổi này không hủy lượt download đã bắt đầu.
3. Mong đợi: `3.1.2-rollback-e2e` tải và boot một lần, báo `intentional P2.5
   rollback test image`/`rolling back`, rồi bootloader trở lại
   `3.1.1-ota-e2e`. Reset thêm một lần; vẫn chạy bản tốt. Kiểm tra điểm danh,
   dữ liệu, audio và upload.
4. Nếu không rollback, hoặc bản lỗi lặp vô hạn, ngừng thử và giữ serial log.
   **Không** flash image lỗi qua USB để cứu máy. Dùng USB nạp lại image tốt
   vào app slot theo hướng dẫn recovery, tuyệt đối không `erase-flash`.

Sau test, giữ `OTA_ENABLED=false`, lưu version cuối, checksum, serial log và
ảnh dashboard/Sheet; cập nhật biên bản `TC-13` trong
`FULL_SYSTEM_ACCEPTANCE_TEST.md`. `P2_5_TEST_GUIDE.md` là tài liệu lịch sử
cho firmware 2.5, không dùng các binary/version cũ trong đó.
