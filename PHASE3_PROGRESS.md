# Phase 3 progress

Cập nhật: 2026-09-30.

## Trạng thái

P3.0 và P3.1 đã hoàn thành ở mức source, host tests và ESP-IDF build. P3.2 đang
chờ nghiệm thu trên DFPlayer Mini, loa và microSD thật.

Người dùng đã chạy sơ bộ luồng với hai học sinh và báo kết quả ổn; đây chưa
thay cho biên bản đủ sáu track, fault tests, soak 30 phút và OTA regression.

## Đã triển khai

- UART1 9600 8N1 trên GPIO41/42, không xung đột camera, USB hoặc console;
- codec packet 10 byte, checksum và parser frame phản hồi;
- UART task + queue non-blocking; camera không chờ âm thanh;
- sáu event/track với cooldown chống spam;
- NVS volume/mute có magic, schema và CRC32;
- maintenance portal có audio status, save config và test track;
- audio là optional peripheral, không thuộc OTA health gate;
- script chuẩn hóa nội dung microSD;
- wiring, Ubuntu và full-flow/fault test guide.

## Automated gate

- toàn bộ Phase 1/2 host regression: PASS;
- DFPlayer packet/checksum tests: PASS;
- maintenance portal HTML/JavaScript test: PASS;
- Phase 3 pin/resource/source test: PASS;
- ESP-IDF 6.0.2 build: PASS;
- app version: 3.0.0-phase3-dev;
- binary size: 0x481b10, còn 0x17e4f0 byte (25%) trong mỗi OTA slot.
- release SHA-256: 1c3737eb1db5660ec9e9d1eeb2c423c048759d0bc751e9feeba61b7c9801814b.

## Physical gate còn lại

1. Chuẩn bị sáu MP3 và thẻ FAT32.
2. Đấu đúng 5V/GND/GPIO41/GPIO42 và SPK1/SPK2.
3. Test đủ sáu track, volume/mute và persistence.
4. Full flow hai học sinh + dashboard.
5. Test thiếu microSD, thiếu DFPlayer và theo dõi brownout/reset 30 phút.
6. OTA regression khi DFPlayer đang cắm.

Chi tiết tại P3_TEST_GUIDE.md. Chỉ đóng Phase 3 sau khi toàn bộ physical gate
pass.
Biên bản full-flow để chốt dự án: FULL_SYSTEM_ACCEPTANCE_TEST.md.
