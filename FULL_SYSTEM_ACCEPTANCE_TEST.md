# Nghiệm thu end-to-end — ESP32-S3 Attendance System (Phase 1–3)

**Trạng thái:** checklist nghiệm thu, không phải biên bản PASS. Chủ dự án đã
xác nhận nhiều luồng phần cứng (loa, điểm danh hai học sinh, đóng buổi và
chạy lâu) hoạt động tốt, nhưng repo không chứa biên bản đã ẩn dữ liệu cá nhân.
OTA/rollback và gửi CSV qua email vẫn cần nghiệm thu thực tế. Không đánh dấu
release hoàn tất chỉ vì bài test tự động chạy xanh.

Các lệnh dùng `git rev-parse --show-toplevel` bên dưới cần chạy từ một thư mục
bên trong bản clone repository.

## 0. Nguyên tắc và phiếu kết quả

- Chạy trên board ESP32-S3-WROOM N16R8 camera tích hợp đã dùng ở các phase trước,
  hai danh tính test ID 1/2, một DFPlayer Mini, loa, microSD và Wi-Fi 2,4 GHz.
- Sao lưu Sheet/ảnh/log cần giữ trước khi tạo dữ liệu test. Dùng session ID
  **có chữ**, ví dụ `E2E-20260930-A` và `E2E-20260930-B`; không tái sử dụng
  Event ID/session cũ. Không xóa danh tính hoặc chạy `erase-flash`.
- Thực hiện trên sheet staging nếu chưa muốn tạo hai buổi học test trong sheet
  chính. Nếu đổi URL/secret sang staging, lưu cấu hình cũ trước và khôi phục
  sau; không chia sẻ secret trong ảnh/log.
- Tạo biên bản riêng và ghi cho **mỗi mã TC**: thời gian, PASS/FAIL, Event ID
  liên quan, ảnh/log chứng minh và lỗi/cách xử lý. Không đẩy tên thật, ảnh mặt,
  embeddings, Wi-Fi password, device/dashboard secret hay URL bí mật lên Git.
- Bất kỳ reset loop, brownout, mất dữ liệu, duplicate trên Sheet hoặc OTA không
  rollback đều là **FAIL**; dừng ca đó, giữ nguyên serial log và không reset/xóa
  dữ liệu để “thử lại cho qua”.

Sao chép bảng này vào biên bản riêng trước khi chạy (đừng sửa kết quả PASS cũ
nếu phải chạy lại; ghi thêm lượt mới):

| Ca | Kết quả (PASS/FAIL/BLOCKED) | Thời gian, Event ID, bằng chứng / ghi chú |
|---|---|---|
| TC-00 Chuẩn bị | | |
| TC-01 Sáu track + lời chào theo tên | | |
| TC-02 Volume/mute | | |
| TC-03 Enroll audio | | |
| TC-04 Present/duplicate | | |
| TC-05 Override/CSV/manual close | | |
| TC-06 Late/filter | | |
| TC-07 Auto close | | |
| TC-08 Heartbeat | | |
| TC-09 Offline outbox/reboot | | |
| TC-10 Offline/online/camera | | |
| TC-11 Thiếu ngoại vi | | |
| TC-12 Soak 30 phút | | |
| TC-13 OTA/rollback | | |
| TC-14 CSV email sau đóng buổi | | |

## 1. Chuẩn bị hệ thống (TC-00, khoảng 15 phút)

1. Trong Apps Script đang chạy, kiểm tra `Code.gs`/`Dashboard.html` khớp
   source hiện tại. Deployment là **Ứng dụng web**, chạy bằng tài khoản chủ sở
   hữu, access **Bất kỳ ai**; mở `<EXEC_URL>` phải thấy JSON health
   `ok:true`, `api_version:2`, `schema_version:"2"`; dashboard ở
   `<EXEC_URL>?view=dashboard`. Không chạy lại `setup`, migration hay
   provision secret chỉ vì test nếu hệ thống đã hoạt động.
2. Kiểm tra đúng **một** trigger `closeExpiredSessions` (mỗi 5 phút),
   `refreshDeviceStatuses` (mỗi 1 phút) và `sendClosedSessionReports` (mỗi
   1 phút, sau khi cấp quyền MailApp). Script Property
   `OTA_ENABLED=false` cho đến ca OTA. Sheet `Students` có ID 1 và 2,
   cùng lớp với metadata trên board, `Active=TRUE`; hai danh tính đã enroll.
3. Khi **đã rút nguồn**, nhìn board với camera ở trên, hai USB-C ở dưới; tìm
   chữ in trên PCB. Giữ cáp FPC camera nguyên trạng. Đấu:

   | ESP32-S3 | DFPlayer Mini |
   |---|---|
   | 5V | VCC |
   | GND | GND |
   | GPIO41 (TX) qua điện trở 1 kΩ | RX |
   | GPIO42 (RX) | TX |
   | — | loa giữa SPK1 và SPK2 |

   Không nối loa xuống GND, không cấp module từ 3V3. Nếu module clone gây
   sụt áp, kiểm tra nguồn và dây trước khi thử tiếp; xem
   `HARDWARE_AND_UBUNTU_GUIDE.md`.
4. Chuẩn bị thẻ FAT32 (tối đa 32 GB) với sáu MP3 câu chung và một MP3 lời
   chào đầy đủ cho mỗi Student ID, không phải file rỗng. Export
   `student-voice-roster.json` tại portal đăng ký, chuẩn bị `student_0001.mp3`
   cho ID 1, `student_0002.mp3` cho ID 2 (mỗi file nói đúng “Xin chào [tên],
   bạn đã điểm danh thành công”).
   Trên Ubuntu xác định đúng mount bằng `lsblk -o NAME,SIZE,MODEL,FSTYPE,MOUNTPOINTS`;
   chạy `./tools/prepare_audio_sd.sh /duong-dan/audio-source /media/$USER/TEN_THE /duong-dan/rieng/student-voice-roster.json`,
   `sync`, eject rồi mới lắp thẻ khi DFPlayer mất nguồn. Mapping:
   `0001` khởi động, `0002` điểm danh thành công, `0003` trùng,
   `0004` chưa có giờ, `0005` enroll, `0006` lỗi có thể phục hồi;
   `1001`/`1002` là lời chào đúng tên ID 1/2. Xem chi tiết tại
   `AUDIO_CLASS_ROSTER_GUIDE.md`.
5. Ubuntu: `ls -l /dev/serial/by-id/`, xác minh symlink đang trỏ tới board.
   Trong `firmware/` chạy:

   ```bash
   eim run "idf.py build" v6.0.2
   eim run "idf.py -p /dev/ttyACM0 flash monitor" v6.0.2
   ```

   Thay `/dev/ttyACM0` bằng symlink thực tế trong `/dev/serial/by-id/`
   nếu khác. **Không `erase-flash`**.
   Để test lời chào theo tên phải nạp bản mới `3.1.0-phase3-ux-dev`; firmware
   `3.0.0-phase3-dev` cũ chỉ phát câu chung. Kỳ vọng boot báo
   `3.1.0-phase3-ux-dev`, camera OK,
   audio UART1 TX=41 RX=42, Wi-Fi/NTP sẵn sàng và vòng nhận diện chạy. Không
   kết luận module hỏng chỉ vì clone không trả UART ACK; âm thanh thực mới là
   phép thử phát.

## 2. Âm thanh và cấu hình bảo trì (TC-01…03, khoảng 15 phút)

1. Khi firmware đang chạy, giữ **BOOT 2,5 giây**, thả ra và chờ board restart
   vào `RobotSetup`; **không** vừa giữ BOOT vừa bấm RST. Kết nối AP và mở
   `http://192.168.4.1/config`. Mật khẩu AP là mật khẩu bàn giao đã cấu hình
   trong `setup_ap_password.h` của bản build này, ở cả lần đầu và maintenance.
   Tắt VPN/mobile data nếu máy tự bỏ AP không có Internet.
2. **TC-01:** bật Audio (âm lượng cố định 30/30), lưu. Phát thử nút trên trang; nếu muốn
   test đủ sáu track ngay trong portal, chạy trên máy đang nối AP:

   ```bash
   for track in 1 2 3 4 5 6; do
     curl -fsS -X POST -d "track=$track" http://192.168.4.1/audio/test
     sleep 3
   done
   ```

   Sau đó nhập ID 1, ID 2 ở nút **Phát thử lời chào có tên** và nghe đúng hai
   tên khác nhau. Đối chiếu **âm thanh nghe được và đúng nội dung**; HTTP OK
   chỉ chứng minh lệnh được nhận, không chứng minh loa/thẻ phát được.
3. **TC-02:** reboot và mở lại portal: Audio vẫn bật, mức âm lượng 30/30.
   Tắt Audio, reboot; trong một session test riêng đang ACTIVE và sau cooldown
   60 giây, thử điểm danh: event vẫn lưu/upload nhưng không phát tiếng. Đóng
   session test đó, bật lại Audio trước TC-04. Không test mute bằng cách
   rút dây loa lúc thiết bị đang cấp nguồn.
4. **TC-03:** nếu cần nghiệm thu track enroll, chỉ enroll một danh tính test
   mới đã chuẩn bị và **không xóa** hai danh tính chính. Track 5 chỉ được phát
   khi metadata đã commit; nếu không muốn tăng roster, ghi `N/A — chưa test
   enroll` và **không** đánh dấu toàn bộ Phase 3 PASS. Kết thúc portal bằng
   nút **Kết thúc buổi đăng ký** để trở về nhận diện.

## 3. Điểm danh, session và dashboard (TC-04…07, khoảng 20 phút)

1. **TC-04:** đăng nhập dashboard, mở **Bắt đầu buổi học mới**, chọn lớp và
   buổi Sáng/Chiều; để các trường nâng cao trống để kiểm tra luồng tối giản.
   Mã buổi, giờ bắt đầu, mốc đi muộn 10 phút và giờ kết thúc phải tự tạo đúng.
   ID 1 nhìn camera một lần. Ghi Event ID
   trong serial. PASS khi local báo saved, cloud báo uploaded, Sheet
   `Attendance` có **đúng một** dòng cùng Event ID, dashboard thấy Có mặt
   và track `1001.mp3` phát **đúng tên ID 1 cùng câu thành công đầy đủ**. Giữ
   ID 1 trong khung trong cooldown 60 giây: không thêm event/row, track 3
   không cắt ngang lời chào hoặc spam.
2. **TC-05:** ID 2 chưa điểm danh nên tổng buổi A (roster đúng hai học sinh)
   là 1 Có mặt + 1 Vắng. Đổi ID 2 sang Có phép, nhập lý do. UI phải báo lưu
   thành công; reload vẫn Có phép, `AuditLog` có `ATTENDANCE_OVERRIDE`
   với actor/reason/before/after, còn `Attendance` không bị sửa/xóa. Xuất
   CSV, mở bằng LibreOffice: tiếng Việt, status, Session ID, Event ID và
   Reason của ID 2 đúng với lý do Có phép; học sinh không điều chỉnh để trống
   Reason. Sau đó giáo viên **đóng buổi A thủ công**; status CLOSED và
   `SESSION_STATUS_CHANGE` có trong AuditLog.
3. **TC-06:** tạo session `E2E-YYYYMMDD-B` trong **Tùy chọn nâng cao** sau khi A
   đã đóng, Starts At =
   hiện tại trừ 5 phút, Late After = hiện tại trừ 1 phút, Ends At = hiện tại
   cộng **3 phút**. Cho ID 2 nhìn camera: đúng một event và trạng thái
   Đi muộn; lời chào `1002.mp3` phải đúng tên ID 2; đừng dùng lại Event ID của A.
   Với mỗi session:
   `Có mặt + Đi muộn + Vắng + Có phép = sĩ số`. Thử lọc lớp/session/ngày,
   tìm ID và mở dashboard trên điện thoại. Chuyển A → B → A nhanh: dữ liệu buổi
   đã xem hiện tạm ngay, thông báo đang đồng bộ xuất hiện, và response cũ không
   được ghi đè buổi mới. Ghi thời gian chuyển lần đầu/lần đã cache.
4. **TC-07:** không đóng B bằng tay. Sau Ends At, **không refresh dashboard
   ngay**; chờ tối đa 5 phút + 1 phút đệm cho trigger chạy. Sau đó kiểm tra
   sheet `Sessions` đã CLOSED và `AuditLog` có
   `SESSION_STATUS_CHANGE`, actor SYSTEM. Nếu chỉ đóng sau khi mở dashboard,
   trigger tự động chưa đạt. Nếu không muốn chờ, để ca này pending — không
   tính PASS.

## 4. Mạng, outbox và camera (TC-08…10, khoảng 10 phút hoạt động + 5 phút chờ)

1. **TC-08:** khi online, quan sát ít nhất ba heartbeat cách nhau khoảng 60
   giây. `Devices`/dashboard có ONLINE, firmware `3.1.0-phase3-ux-dev`,
   Last Seen mới, RSSI, heap/PSRAM, pending và sync latency hợp lệ. Camera
   vẫn nhận diện khi heartbeat/upload chạy.
2. **TC-09:** ngắt **Internet/WAN nhưng giữ router Wi-Fi 2,4 GHz hoạt động**
   sau khi board đã đồng bộ NTP; tránh reboot trước khi tạo event mới. Dùng
   một học sinh ngoài cooldown/session test còn ACTIVE (tạo session C nếu
   cần), ghi Event ID local, xác nhận pending tăng và Sheet chưa có event.
   Nhấn RST **sau khi event đã lưu**, bật Internet lại; chờ uploader retry.
   PASS khi cùng Event ID xuất hiện đúng một lần, pending về 0, không mất face
   database. Nếu đã reboot khi giờ chưa hợp lệ, chờ NTP lại trước khi tạo
   event mới; event cũ trong outbox vẫn phải tồn tại.
3. **TC-10:** lần nữa ngắt Internet sau heartbeat ONLINE; chờ hơn 180 giây,
   cộng tối đa một chu kỳ trigger 1 phút: Devices thành OFFLINE. Bật lại
   Internet, chờ heartbeat ONLINE. Trong lúc retry, ID còn lại vẫn nhận diện,
   không camera freeze, panic, watchdog hoặc brownout. Chụp trạng thái
   ONLINE → OFFLINE → ONLINE và log backoff.

## 5. Ngoại vi lỗi, độ ổn định, OTA và email (TC-11…14)

1. **TC-11 — thiếu microSD/module:** tắt nguồn trước khi tháo thẻ; boot lại,
   camera/điểm danh/local outbox/upload vẫn bình thường dù không có âm thanh.
   Tắt nguồn lần nữa, tháo cả DFPlayer rồi boot: chức năng cốt lõi vẫn chạy,
   không reset loop. Tắt nguồn, gắn lại đầy đủ; test lại track `1001` có tên. **Không**
   hot-plug microSD, module hoặc dây loa khi đang có nguồn.
2. **TC-12 — soak 30 phút:** hệ thống đầy đủ chạy liên tục, audio không spam,
   heartbeat đều, camera không tăng lỗi JPEG bất thường, không reset,
   brownout hay mất Wi-Fi dai dẳng. Ghi uptime đầu/cuối và số event/số row.
3. **TC-13 — OTA regression khi DFPlayer đã cắm:** làm trên staging/thiết bị
   kiểm thử hoặc sau khi đã sao lưu và có HTTPS host trả **raw binary**.
   Bản firmware mới là `3.1.0-phase3-ux-dev`; **hai binary
   `3.0.1-e2e-test`/`3.0.2-rollback-test` được build trước thay đổi này đã
   lỗi thời, không dùng để phát hành/test build mới**. Build candidate khác
   version từ source mới, ví dụ:

   ```bash
   cd "$(git rev-parse --show-toplevel)/firmware"
   eim run "idf.py -B build-ota-ux-e2e -DCDP_FIRMWARE_VERSION_OVERRIDE=3.1.1-e2e-test build" v6.0.2
   cd ..
   ./tools/package_ota.sh 3.1.1-e2e-test https://HOST/robot_attendance-3.1.1-e2e-test.bin firmware/build-ota-ux-e2e/robot_attendance.bin firmware/build-ota-ux-e2e/ota-manifest.json
   ```

   Upload **đúng** binary đó; lấy `version/url/size/sha256` từ manifest vào
   Script Properties `OTA_VERSION/OTA_URL/OTA_SIZE/OTA_SHA256`. Đảm bảo URL
   tải được không cần login, không trả HTML. Chỉ khi sẵn sàng mới bật
   `OTA_ENABLED=true`. Reset để kiểm tra sớm (firmware kiểm tra sau khoảng
   60 giây; định kỳ lâu hơn). PASS: image mới được xác minh, boot, qua health
   gate, nhận diện và audio trở lại, data/Sheet còn nguyên, dashboard thấy
   version mới. Reset lần nữa và xác nhận image mới vẫn VALID.
4. Kiểm tra ba ca lỗi OTA trên **thiết bị thử**, không trong buổi học thật:
   (a) dùng SHA-256 sai cho candidate khác version, xác nhận boot partition
   không đổi; (b) dùng SHA đúng rồi ngắt nguồn có kiểm soát khi đang tải,
   tắt `OTA_ENABLED` trước khi cấp nguồn lại và xác nhận bản tốt vẫn chạy;
   (c) chỉ sau khi candidate tốt đã được xác nhận, tạo image rollback-test
   với `CDP_OTA_ROLLBACK_TEST=ON` và version mới hơn. Phân phối image lỗi qua
   OTA, **không flash qua USB làm image nền**. Tắt OTA khi image bắt đầu tải;
   image lỗi phải boot một lần rồi tự quay về bản tốt. Reset lại và kiểm tra
   danh tính, outbox, âm thanh và upload vẫn còn. Nếu không rollback, dừng
   thử và giữ serial log. Sau cùng đặt `OTA_ENABLED=false` và ghi version,
   checksum thực. Nếu thiếu HTTPS host hoặc điều kiện ngắt nguồn an toàn,
   ghi **BLOCKED**, không coi dự án đã đóng.

5. **TC-14 — gửi CSV qua email:** sao lưu Sheet, cập nhật cả `Code.gs` và
   `Dashboard.html`, chạy `migrateToV2` và `installPhase2Automation`, cấp
   quyền MailApp rồi cập nhật deployment hiện tại. Dùng email test thuộc
   quyền kiểm soát của bạn. Tạo buổi A có một lượt điểm danh và một điều
   chỉnh `Có phép` kèm lý do; đóng tay. `Report Status` phải chuyển
   `PENDING` → `SENT`, có thời điểm gửi và đúng một email đính kèm CSV với
   `Reason`/Event ID khớp dashboard. Chạy trigger lại không được gửi trùng.
   Tạo buổi B, để trigger tự đóng rồi kiểm tra tương tự. Kiểm tra email sai
   định dạng bị từ chối; mô phỏng lỗi gửi trên staging (không cố ý tiêu quota
   thật), xác nhận `FAILED` và mốc retry. Nếu kẹt `SENDING`, kiểm tra hộp thư
   và AuditLog trước khi xử lý để tránh gửi hai lần. Báo cáo gửi khi robot còn
   pending events có thể thiếu lượt offline; chỉ chốt khi pending về 0.

## 6. Gate đóng dự án và bằng chứng bàn giao

| Nhóm | Cần có trước khi đánh dấu DONE |
|---|---|
| Firmware/backend | Host + Apps Script tests + ESP-IDF build PASS, `git diff --check` sạch |
| Luồng người dùng | TC-00…07 PASS, hai Event ID được đối chiếu local/Sheet/dashboard/AuditLog |
| Độ bền | TC-08…10 PASS, outbox đúng một lần, ONLINE → OFFLINE → ONLINE |
| Phase 3 | TC-01…03, TC-11…12 PASS, sáu track đúng, ngoại vi optional, 30 phút ổn định |
| Release | TC-13 và TC-14 PASS; fault/rollback OTA P2.5 và email thật có bằng chứng |

Chạy kiểm thử tự động tại repo root:

```bash
bash tests/host/run_tests.sh
node tests/apps_script/test_code.js
node tests/apps_script/test_dashboard_html.js
cd firmware
eim run "idf.py build" v6.0.2
```

Các test JavaScript cần Node.js; nếu binary không nằm trong `PATH`, đặt
`NODE_BIN` trỏ tới executable đã cài và dùng cùng executable đó cho hai
lệnh Apps Script. Lưu output cùng biên bản (redact secret). Sau khi **tất cả**
gate có bằng chứng, chốt firmware/version/checksum thực đã test trong biên bản
riêng (không chứa dữ liệu cá nhân) rồi mới gắn nhãn release. Nếu còn
FAIL/BLOCKED, ghi rõ và sửa trước khi phát hành.
