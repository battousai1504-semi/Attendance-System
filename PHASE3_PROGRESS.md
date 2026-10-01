# Phase 3 progress

Cập nhật: 2026-10-01.

## Bổ sung 2026-10-01 — lời chào theo tên và UX giáo viên

Sau khi full-flow cũ với hai học sinh đạt, phát hiện track 2 chỉ nói câu thành
công chung, chưa đọc tên. Bản `3.1.0-phase3-ux-dev` ánh xạ Student ID 1…50
sang `/mp3/1001.mp3`…`1050.mp3`; mỗi file chứa câu trọn vẹn “Xin chào [tên],
bạn đã điểm danh thành công”. Công cụ SD nhận roster JSON tải từ portal,
kiểm tra đủ file trước khi chép và từ chối clip ID cũ lạc roster. Nếu DFPlayer
báo file thiếu, firmware cố phát track 2 chung; đó là degraded mode, không
phải kết quả đạt yêu cầu đọc tên.

Dashboard mới đưa chọn/bắt đầu buổi và danh sách điểm danh lên trước, tự tạo
mã buổi và mốc giờ mặc định; các trường cấu hình nâng cao, lọc lịch sử và chẩn
đoán thiết bị được thu gọn. Host/Apps Script tests và ESP-IDF build đã PASS ở
mức source. **Chưa flash bản mới, chưa nghe hai tên trên loa thật, chưa deploy
Dashboard.html mới**, nên tính năng bổ sung chưa được nghiệm thu phần cứng/UI.
Hai binary OTA test `3.0.1`/`3.0.2` đã chuẩn bị trước đó là bản cũ, không
được phát hành như bản nâng cấp hiện tại; TC-13 vẫn BLOCKED.

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

## Automated gate của bản 3.0.0 trước bổ sung

- toàn bộ Phase 1/2 host regression: PASS;
- DFPlayer packet/checksum tests: PASS;
- maintenance portal HTML/JavaScript test: PASS;
- Phase 3 pin/resource/source test: PASS;
- ESP-IDF 6.0.2 build: PASS;
- app version: 3.0.0-phase3-dev;
- binary size: 0x481b10, còn 0x17e4f0 byte (25%) trong mỗi OTA slot.
- release SHA-256: 1c3737eb1db5660ec9e9d1eeb2c423c048759d0bc751e9feeba61b7c9801814b.

## Physical gate còn lại

1. Chuẩn bị sáu câu chung, lời chào theo từng Student ID và thẻ FAT32.
2. Đấu đúng 5V/GND/GPIO41/GPIO42 và SPK1/SPK2.
3. Test đủ sáu track, volume/mute và persistence.
4. Full flow hai học sinh + dashboard.
5. Test thiếu microSD, thiếu DFPlayer và theo dõi brownout/reset 30 phút.
6. OTA regression khi DFPlayer đang cắm.

Chi tiết tại P3_TEST_GUIDE.md. Chỉ đóng Phase 3 sau khi toàn bộ physical gate
pass.
Biên bản full-flow để chốt dự án: FULL_SYSTEM_ACCEPTANCE_TEST.md.
