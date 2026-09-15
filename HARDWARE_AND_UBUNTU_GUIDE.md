# Phần cứng thực tế và quy trình Ubuntu

## 1. Nhận diện đúng board

Board mục tiêu là **ESP32-S3-WROOM N16R8 camera board**, loại hai cổng USB-C,
camera gắn sẵn qua socket FPC và có hai nút `RST`/`BOOT`. Đây là thiết kế dùng
pinout tương thích Freenove/`CAMERA_MODEL_ESP32S3_EYE`; một số board bán trên
thị trường là clone nên tên hãng có thể không được in trên PCB.

**Không nối dây GPIO giữa camera và board.** Toàn bộ bus camera đã được PCB nối
từ socket FPC đến ESP32-S3. Chỉ cần:

1. Ngắt USB và mọi nguồn điện.
2. Kiểm tra cáp camera nằm thẳng, cắm hết hành trình vào socket.
3. Kiểm tra lẫy đen của socket đã khóa đều hai bên.
4. Không tháo/lắp cáp FPC khi board đang có điện.

Ánh xạ dưới đây chỉ là **đường mạch nội bộ để đối chiếu firmware**, không phải
sơ đồ yêu cầu người dùng câu jumper:

| Tín hiệu camera nội bộ | GPIO |
|---|---:|
| XCLK | 15 |
| SIOD / SDA | 4 |
| SIOC / SCL | 5 |
| Y9 / D7 | 16 |
| Y8 / D6 | 17 |
| Y7 / D5 | 18 |
| Y6 / D4 | 12 |
| Y5 / D3 | 10 |
| Y4 / D2 | 8 |
| Y3 / D1 | 9 |
| Y2 / D0 | 11 |
| VSYNC | 6 |
| HREF | 7 |
| PCLK | 13 |
| PWDN / RESET | Không dùng (`-1`) |

Các chân onboard khác:

| Chức năng | GPIO | Ghi chú |
|---|---:|---|
| LED trạng thái đơn | 2 | LED cạnh chữ `IO2`, firmware dùng LED này |
| RGB LED | 48 | WS2812; không điều khiển bằng mức HIGH/LOW thông thường |
| BOOT / maintenance | 0 | Giữ 2,5 giây lúc app khởi động để mở enrollment |
| USB-UART | TX 43, RX 44 | Nối sẵn tới chip USB-UART trên board |
| USB-OTG | D- 19, D+ 20 | Nối sẵn tới cổng USB native |

## 2. Chọn đúng cổng USB-C

Board có hai cổng khác nhau, không phải hai cổng tương đương:

- **USB-UART**: dùng để flash và xem log trong Phase 1. Cổng này ổn định hơn và
  có mạch tự đưa chip vào download mode. Khi cầm board đúng chiều như ảnh,
  antenna ở trên và USB ở dưới, đây thường là cổng bên trái, gần nút `RST`.
- **USB-OTG/USB**: nối trực tiếp tới USB native của ESP32-S3. Phase 1 không cần
  dùng cổng này.

Hãy ưu tiên chữ `UART`/`USB-UART` in trên PCB hoặc mặt sau hơn vị trí trái/phải,
vì clone có thể thay đổi silkscreen. Chỉ cắm **một cáp USB-UART** trong lần test
đầu; không cần cấp thêm 5 V, 3V3 hay GND bằng jumper.

## 3. Cài ESP-IDF 6.0.2 trên Ubuntu

```bash
echo "deb [trusted=yes] https://dl.espressif.com/dl/eim/apt/ stable main" \
  | sudo tee /etc/apt/sources.list.d/espressif.list
sudo apt update
sudo apt install eim-cli
eim install -i v6.0.2
```

Mỗi phiên làm việc mở môi trường IDF bằng:

```bash
eim shell v6.0.2
```

Nếu EIM yêu cầu installation ID, chạy `eim list` rồi dùng ID được hiển thị.

Nếu `eim shell` báo `has no activation script set`, lần cài trước đã bị dở
dang. `eim select` chỉ chọn bản cài, không sửa phần cài đặt còn thiếu.
Repair đúng thư mục IDF hiện có và ghim rõ phiên bản `v6.0.2`:

```bash
mkdir -p "$HOME/.cache/esp-idf-tmp"
TMPDIR="$HOME/.cache/esp-idf-tmp" \
eim fix -p "$HOME/.espressif/v6.0.2/esp-idf" \
  -i v6.0.2 -t esp32s3 -n true \
  --cleanup true
eim list
eim select v6.0.2
eim shell v6.0.2
idf.py --version
```

Nếu repair báo `Quota exceeded`, kiểm tra cả filesystem chứa bản cài và vùng
tạm. Ổ đĩa còn trống không đồng nghĩa với việc quota của user còn đủ:

```bash
df -h /tmp "$HOME/.espressif" "$HOME/.cache/esp-idf-tmp"
findmnt -T /tmp
findmnt -T "$HOME/.espressif"
```

Lệnh repair trên đặt `TMPDIR` vào thư mục trên ổ đĩa thay vì mặc định `/tmp`
(Ubuntu có thể mount `/tmp` bằng tmpfs kèm quota). Nếu home cũng có quota thì
cần giải quyết quota đó; đổi `TMPDIR` không tự bỏ qua mọi giới hạn lưu trữ.
Không xóa cache trình duyệt hoặc toolchain khi chưa xác định nơi gây lỗi.
EIM có thể cài thêm tool ngoài ESP32-S3; không giả định `-t esp32s3` sẽ loại
bỏ toàn bộ Clang/RISC-V.

Chỉ chuyển sang build khi repair thành công và `idf.py --version` xác nhận
`ESP-IDF v6.0.2`. Tham khảo cú pháp [EIM CLI chính thức](https://docs.espressif.com/projects/idf-im-ui/en/latest/cli_commands.html).

Đã kiểm tra trực tiếp trên Dang-Laptop ngày 2026-09-15: repair với lệnh trên
hoàn tất, EIM có trạng thái `finished` và đường dẫn activation script hợp lệ;
`idf.py --version` trả về `ESP-IDF v6.0.2`, compiler Xtensa chạy được và
`python -m pip check` báo `No broken requirements found.` Đây là kiểm tra
môi trường phát triển, chưa phải kết quả test firmware trên board.

EIM có thể báo không đủ quyền copy `60-openocd.rules` vào `/etc/udev/rules.d`.
Đó là quyền USB phục vụ OpenOCD/JTAG, không phải lỗi activation Python;
không cần chạy lại toàn bộ installer bằng `sudo` để nạp qua USB-UART.

## 4. Cấp quyền serial trên Ubuntu

Thực hiện một lần, sau đó đăng xuất và đăng nhập lại:

```bash
sudo usermod -a -G dialout "$USER"
```

Cắm cáp vào cổng **USB-UART**, rồi tìm thiết bị:

```bash
ls -l /dev/serial/by-id/ 2>/dev/null
python3 -m serial.tools.list_ports -v  # tùy chọn; bỏ qua nếu thiếu pyserial
```

Board dùng chip CH343 có thể xuất hiện dưới `/dev/ttyACM0`; một số clone dùng
bridge khác có thể là `/dev/ttyUSB0`. Dùng đúng port vừa xuất hiện khi cắm board.
Chỉ cần symlink trong `/dev/serial/by-id/` đã trỏ tới một `/dev/tty*` là đủ;
không cần cài `pyserial` chỉ để chạy lệnh kiểm tra tùy chọn trên.

## 5. Tạo cấu hình local

```bash
cd /home/battousai1504/VScode/Projects/Attendance-System/firmware/main
cp wifi_time_config.h.example wifi_time_config.h
cp cloud_config.h.example cloud_config.h
```

Sửa hai file vừa tạo:

- `wifi_time_config.h`: Wi-Fi 2,4 GHz, SSID và mật khẩu thật.
- `cloud_config.h`: Apps Script URL kết thúc bằng `/exec`, token ngẫu nhiên ít
  nhất 32 ký tự, device ID và mật khẩu enrollment AP dài 8–63 ký tự.

Hai file này bị Git bỏ qua và không được commit.

## 6. Build và flash lần đầu

Từ ESP-IDF shell:

```bash
cd /home/battousai1504/VScode/Projects/Attendance-System/firmware
idf.py set-target esp32s3
idf.py build
```

**Chỉ trong lần đầu chuyển từ firmware của nhà bán hàng sang dự án này**, xóa
flash để loại bỏ partition/data cũ không tương thích rồi nạp firmware:

```bash
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash monitor
```

Thay `/dev/ttyACM0` bằng port đã tìm được. Sau lần đầu, cập nhật thông thường
chỉ dùng:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Không dùng `erase-flash` cho update thường vì nó xóa face embeddings, danh sách
học sinh, lịch sử local và upload outbox. Thoát monitor bằng `Ctrl+]`.

Nếu USB-UART không tự vào download mode:

1. Giữ `BOOT`.
2. Nhấn rồi thả `RST` trong khi vẫn giữ `BOOT`.
3. Thả `BOOT`.
4. Chạy lại lệnh flash.

Đừng tiếp tục giữ BOOT khi app đã chạy, nếu không firmware sẽ hiểu là yêu cầu
mở maintenance enrollment.

## 7. Quy trình smoke test trên board

### A. Khởi động và camera

Sau `flash monitor`, chờ các marker:

```text
Robot Điểm Danh 1.2.1-phase1-rc1
Camera OK (VGA, PSRAM)
student_db ready
Face models ready
```

Pass khi không có `Camera init failed`, panic, brownout hoặc reset loop. Nếu có
lỗi camera, rút USB trước rồi kiểm tra lại cáp FPC và lẫy socket.

### B. Enrollment lần đầu

Thiết bị sạch tự mở enrollment AP. Điện thoại/laptop kết nối SSID đã cấu hình;
nếu thiết bị báo Wi-Fi không có Internet, chọn giữ kết nối. Mở:

```text
http://192.168.4.1
```

Lần lượt kiểm tra: không có mặt, nhiều mặt, một mặt hợp lệ, hoàn tác ID cuối,
đăng ký lại, sau đó bấm **Kết thúc buổi đăng ký**. Dùng ánh sáng đều và đặt mặt
cách camera khoảng 40–80 cm.

### C. Wi-Fi, NTP và nhận diện

Sau khi kết thúc enrollment, chờ log Wi-Fi và `NTP time synchronized`. Đưa đúng
khuôn mặt vào camera. Một event hợp lệ phải có:

```text
ATTENDANCE SAVED: ... | ID=... | FACE | event=...
ĐIỂM DANH: ... conf=...
```

LED đơn `IO2` chớp hai lần. Đưa lại cùng khuôn mặt trong 60 giây phải bị báo
duplicate và không tạo hàng Google Sheets thứ hai.

### D. Cloud và offline retry

Xác nhận `Uploaded event=...` và Event ID trên Google Sheet khớp log. Sau đó
tắt Wi-Fi, tạo thêm event sau khi hết cooldown, reboot board khi mạng vẫn tắt,
bật Wi-Fi lại và xác nhận pending event chỉ xuất hiện một lần trên Sheet.

### E. Reboot và maintenance

Nhấn `RST` bình thường: danh tính phải còn và board vào recognition. Để mở lại
enrollment mà không xóa dữ liệu, giữ `BOOT`, nhấn/thả `RST`, tiếp tục giữ BOOT
ít nhất 2,5 giây rồi thả khi log báo maintenance enrollment.

Checklist nghiệm thu đầy đủ và các bài power-loss/TLS nằm trong
`PHASE1_TEST_GUIDE.md`.

## Nguồn đối chiếu phần cứng

- [Freenove ESP32-S3-WROOM board repository](https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board)
- [Camera pinout tương thích ESP32S3_EYE](https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board/blob/main/C/Sketches/Sketch_07.2_As_VideoWebServer/camera_pins.h)
- [Phân biệt USB-UART và USB-OTG](https://docs.freenove.com/projects/fnk0082/en/latest/fnk0082/codes/C/35_USB.html)
- [ESP-IDF: build, flash và monitor trên Linux](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/linux-macos-start-project.html)
