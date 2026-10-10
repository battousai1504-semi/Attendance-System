# P2.5 — OTA và rollback: hướng dẫn nghiệm thu trên board

> Tài liệu lịch sử cho firmware Phase 2. Các version `2.5.x` bên dưới là ví dụ
> của mốc đó, **không** phải image hiện tại. Với firmware Phase 3, dùng
> `FULL_SYSTEM_ACCEPTANCE_TEST.md` và tạo candidate `3.0.x` riêng trước khi
> bật OTA; không phát hành binary cũ từ ví dụ này.

P2.5 đổi firmware sang hai app slot nhưng giữ nguyên tuyệt đối vùng NVS tại
`0x9000` và SPIFFS `faces` tại `0x610000`. Vì vậy lần chuyển từ P2.4 sang P2.5
phải flash **không erase-flash** để giữ cấu hình, học sinh, face embeddings,
lịch sử local và outbox.

## 1. Chuẩn bị Apps Script

1. Chép `google_apps_script/Code.gs` mới lên project hiện tại.
2. Update Web App deployment đang dùng. Schema Sheet không đổi nên không cần
   chạy lại migration chỉ vì P2.5.
3. Trong Script Properties, tạo `OTA_ENABLED=false`. Chưa tạo hoặc chưa bật
   manifest trong lần flash nền đầu tiên.
4. Giữ nguyên `SPREADSHEET_ID`, device secret và deployment `/exec` hiện tại.

Apps Script không lưu file firmware. Nó chỉ trả manifest đã xác thực cho đúng
device; binary phải được đặt tại một URL HTTPS trực tiếp, trả raw `.bin` và
không yêu cầu đăng nhập/cookie/trang HTML.

## 2. Flash nền P2.5 một lần qua USB-UART

```bash
cd "$(git rev-parse --show-toplevel)/firmware"
ls -l /dev/serial/by-id/
eim run "idf.py build" v6.0.2
eim run "idf.py -p /dev/ttyACM0 flash monitor" v6.0.2
```

Thay port nếu symlink của board trỏ sang tên khác. **Không chạy
`erase-flash`**. Lệnh flash P2.5 ghi bootloader, partition table, `ota_0` và
`ota_data_initial.bin`; nó không ghi lên NVS hoặc partition `faces`.

Marker cần thấy:

```text
Robot Điểm Danh 2.5.0-phase2-dev
Camera OK (VGA, PSRAM)
P2.5 OTA ready: current=2.5.0-phase2-dev
```

Sau boot, kiểm tra lại số học sinh/khuôn mặt, mở maintenance portal và thử một
lượt điểm danh. Pass khi toàn bộ dữ liệu P2.4 còn nguyên và event vẫn lên Sheet
đúng một lần.

## 3. Tạo OTA candidate có version riêng

Không sửa source chỉ để test version. Build sang thư mục riêng:

```bash
cd "$(git rev-parse --show-toplevel)/firmware"
eim run "idf.py -B build-ota-test -DCDP_FIRMWARE_VERSION_OVERRIDE=2.5.1-ota-test build" v6.0.2
cd ..
./tools/package_ota.sh \
  2.5.1-ota-test \
  https://HOST/robot_attendance-2.5.1-ota-test.bin \
  firmware/build-ota-test/robot_attendance.bin \
  firmware/build-ota-test/ota-manifest.json
```

Upload đúng file `.bin` lên `https://HOST/...`. Đối chiếu `size` và `sha256`
trong JSON vừa tạo. Không dùng URL trang preview/chia sẻ; mở URL trong cửa sổ
ẩn danh phải tải đúng binary, không trả HTML đăng nhập.

Đặt bốn Script Properties theo JSON:

- `OTA_VERSION=2.5.1-ota-test`
- `OTA_URL=https://HOST/robot_attendance-2.5.1-ota-test.bin`
- `OTA_SIZE=<số byte>`
- `OTA_SHA256=<64 ký tự hex>`

Chưa bật `OTA_ENABLED` cho đến khi làm từng ca kiểm thử dưới đây. Thay đổi
Script Properties không cần tạo deployment version mới.

## 4. Từ chối checksum sai

1. Đổi một ký tự trong `OTA_SHA256`, đặt `OTA_ENABLED=true`.
2. Nhấn RST để bộ kiểm tra chạy sau khoảng 60 giây; mở monitor:

```bash
cd "$(git rev-parse --show-toplevel)/firmware"
eim run "idf.py -p /dev/ttyACM0 monitor" v6.0.2
```

3. Chờ tải hết và xác nhận log có:

```text
OTA SHA-256 verification failed; boot partition unchanged
OTA 2.5.1-ota-test rejected
```

4. Reset lại: version vẫn là `2.5.0-phase2-dev`, dữ liệu và điểm danh vẫn chạy.

Ca này cố ý tải cả file rồi mới phát hiện hash; đó là cách xác minh chính xác
bytes đã ghi xuống flash. Partition đang chạy không bị thay đổi.

## 5. Ngắt nguồn giữa lúc tải

1. Sửa lại SHA đúng, giữ `OTA_ENABLED=true`, reset board.
2. Khi log đang ở khoảng 10–90%, rút USB-UART.
3. Cắm lại và monitor. Board phải boot `2.5.0-phase2-dev`; không reset loop,
   không mất dữ liệu.
4. Cho tải lại từ đầu. OTA không resume phần dở, nhưng slot đang chạy luôn an
   toàn vì boot partition chỉ đổi sau khi tải đủ và xác minh xong.

## 6. Happy path và health confirmation

Giữ manifest đúng, reset và không ngắt nguồn. Trình tự pass:

```text
Downloading OTA 2.5.1-ota-test ...
OTA download 10%
...
OTA 2.5.1-ota-test verified and selected; restarting
Robot Điểm Danh 2.5.1-ota-test
Camera OK (VGA, PSRAM)
OTA health gate passed; running image marked VALID
```

Sau đó:

1. điểm danh hai học sinh;
2. xác nhận không có dòng trùng trên Sheet;
3. xác nhận dashboard báo firmware `2.5.1-ota-test`;
4. reset thêm một lần và kiểm tra vẫn boot candidate đã được đánh dấu `VALID`.

## 7. Chứng minh automatic rollback

Tạo image kiểm thử riêng. Cờ dưới đây cố ý gọi health failure trước khi image
được đánh dấu hợp lệ; tuyệt đối không dùng cờ này cho release:

```bash
cd "$(git rev-parse --show-toplevel)/firmware"
eim run "idf.py -B build-rollback-test -DCDP_FIRMWARE_VERSION_OVERRIDE=2.5.2-rollback-test -DCDP_OTA_ROLLBACK_TEST=ON build" v6.0.2
cd ..
./tools/package_ota.sh \
  2.5.2-rollback-test \
  https://HOST/robot_attendance-2.5.2-rollback-test.bin \
  firmware/build-rollback-test/robot_attendance.bin \
  firmware/build-rollback-test/ota-manifest.json
```

Upload binary, thay bốn property bằng manifest mới và reset. Kết quả pass:

1. `2.5.2-rollback-test` tải, xác minh và boot một lần;
2. log có `intentional P2.5 rollback test image` và `rolling back`;
3. bootloader tự quay về `2.5.1-ota-test`;
4. camera, nhận diện, face database và upload vẫn bình thường.

Nếu image hỏng sớm đến mức chưa chạy được `app_main`, bootloader vẫn rollback
khi image `PENDING_VERIFY` bị reset/crash. Test flag là cách tái hiện có kiểm
soát, không cần phá binary thật.

## 8. Kiểm tra downgrade

Đặt manifest về binary nền `2.5.0-phase2-dev` với đúng URL/size/SHA rồi reset
board đang chạy `2.5.1-ota-test`. Firmware chấp nhận mọi version khác current,
nên downgrade phải hoàn tất và qua cùng health gate. Sau khi chứng minh xong,
có thể OTA trở lại version muốn giữ.

## 9. Đóng Phase 2

Sau toàn bộ test:

1. đặt `OTA_ENABLED=false` để không phát hành ngoài ý muốn;
2. lưu log của checksum rejection, power-cut recovery, successful update và
   automatic rollback;
3. lưu ảnh Dashboard/Devices và hai lượt điểm danh sau OTA;
4. chạy lần cuối:

```bash
bash tests/host/run_tests.sh
node tests/apps_script/test_code.js
node tests/apps_script/test_dashboard_html.js
cd firmware && eim run "idf.py build" v6.0.2
```

Phase 2 chỉ được đóng sau khi bốn ca phần cứng ở trên đều pass. Source và
automated gate không thể thay thế thao tác ngắt nguồn/rollback trên board thật.
