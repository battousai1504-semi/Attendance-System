# Kế hoạch Phase 3 — DFPlayer Mini và phản hồi âm thanh

## 1. Mục tiêu

Phase 3 bổ sung phản hồi bằng giọng nói cho robot điểm danh mà không làm thay
đổi tính đúng đắn của camera, lưu attendance, upload, dashboard hay OTA.

Nguyên tắc bắt buộc:

- DFPlayer là ngoại vi tùy chọn; thiếu module, loa hoặc microSD không được làm
  robot dừng điểm danh;
- camera không gọi UART trực tiếp. Mọi âm thanh đi qua queue riêng và API
  notification không chờ;
- UART audio không dùng GPIO camera, GPIO0/2, USB GPIO19/20 hoặc cặp TX/RX
  phục vụ nạp code và monitor;
- volume và trạng thái bật/tắt tồn tại qua reboot;
- file âm thanh có mapping cố định, review được.

## 2. Kiến trúc đã chọn

- UART1, 9600 baud, 8 data bits, no parity, 1 stop bit.
- ESP32-S3 TX GPIO41 đi qua điện trở nối tiếp 1 kΩ tới DFPlayer RX.
- ESP32-S3 RX GPIO42 nhận từ DFPlayer TX.
- Task dfplayer sở hữu UART và queue 8 phần tử.
- Code nhận diện chỉ gọi audio_feedback_notify() với timeout bằng 0.
- Cấu hình enabled/volume nằm trong NVS namespace audio_cfg, có magic, schema
  version và CRC32.
- Driver tự đóng/mở packet 10 byte và kiểm tra checksum frame phản hồi.
- Parser UART giữ phần frame chưa đủ giữa nhiều lần đọc và tự resync khi có
  byte rác.
- Maintenance portal cung cấp status, volume 0–30, bật/tắt và phát thử.

Audio không nằm trong health gate của OTA. Một bản firmware tốt không bị
rollback chỉ vì ngoại vi âm thanh chưa cắm.

## 3. Mapping file

| Track | File trên microSD | Sự kiện | Nội dung gợi ý |
|---:|---|---|---|
| 1 | /mp3/0001.mp3 | Hệ thống sẵn sàng | Hệ thống điểm danh đã sẵn sàng |
| 2 | /mp3/0002.mp3 | Điểm danh thành công | Điểm danh thành công |
| 3 | /mp3/0003.mp3 | Điểm danh trùng | Bạn đã điểm danh rồi |
| 4 | /mp3/0004.mp3 | Chưa có thời gian chuẩn | Chưa đồng bộ thời gian, vui lòng thử lại |
| 5 | /mp3/0005.mp3 | Enroll thành công | Đăng ký khuôn mặt thành công |
| 6 | /mp3/0006.mp3 | Lỗi có thể phục hồi | Hệ thống đang gặp lỗi, vui lòng thử lại |

Duplicate bị giới hạn một câu mỗi 5 giây; cảnh báo thời gian/lỗi bị giới hạn
một câu mỗi 10 giây để tránh spam và cắt liên tục file đang phát.

## 4. Milestone

### P3.0 — Hardware và protocol

- khóa GPIO41/42 và tài liệu dây;
- codec packet/checksum DFPlayer có unit test;
- UART task, RX parser và lỗi ngoại vi không fatal.

### P3.1 — Product integration

- âm thanh ready, attendance success, duplicate, time unavailable, enroll và
  recoverable error;
- NVS volume/mute;
- maintenance portal status/config/test;
- chuẩn hóa bộ file microSD.

### P3.2 — Acceptance

- test đủ sáu track;
- full flow hai học sinh;
- volume/mute tồn tại sau reboot;
- tháo microSD và tháo DFPlayer không làm gián đoạn attendance;
- chạy đồng thời camera, upload, dashboard và OTA regression.

Source và automated gate của P3.0–P3.1 hoàn thành. P3.2 cần nghiệm thu trên
đúng DFPlayer/loa/microSD thật theo P3_TEST_GUIDE.md.
