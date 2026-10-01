# Phase 3 — Hướng dẫn đấu dây và test DFPlayer Mini trên Ubuntu

Chạy các lệnh `git rev-parse --show-toplevel` từ một thư mục bên trong bản clone.

## 1. Linh kiện

- board ESP32-S3-WROOM N16R8 camera tích hợp đang dùng;
- DFPlayer Mini;
- loa thụ động đi kèm, tối đa 3 W;
- microSD tối đa 32 GB, FAT16/FAT32;
- một điện trở 1 kΩ cho đường ESP TX → DFPlayer RX;
- dây dupont;
- khuyến nghị tụ 470–1000 µF đặt sát VCC/GND DFPlayer nếu nguồn có nhiễu.

Tắt nguồn và rút cả hai cáp USB trước khi đấu dây.

## 2. Sơ đồ kết nối đúng

Nhìn board camera đúng chiều như ảnh đã gửi: camera ở trên, hai cổng USB-C ở
dưới. Dùng chân có chữ in trên PCB, không đếm thứ tự chân bằng mắt.

| ESP32-S3 camera board | Linh kiện trung gian | DFPlayer Mini |
|---|---|---|
| 5V | dây trực tiếp | VCC |
| GND | dây trực tiếp | GND |
| GPIO41 | điện trở nối tiếp 1 kΩ | RX |
| GPIO42 | dây trực tiếp | TX |
| — | loa đỏ/đen | SPK1 và SPK2 |

Quy tắc quan trọng:

- TX/RX phải đấu chéo: ESP TX41 → DF RX; ESP RX42 ← DF TX.
- Loa nối giữa SPK1 và SPK2, không nối một đầu loa xuống GND.
- Không cấp DFPlayer từ chân 3V3; dùng 5V của board.
- Không dùng GPIO19/20 vì đó là USB D-/D+ của ESP32-S3.
- Không dùng hai chân in TX/RX ở đầu board để việc flash/monitor không bị
  DFPlayer can thiệp.
- Tụ lọc nếu dùng: cực dương vào VCC, cực âm vào GND.

## 3. Chuẩn bị microSD trên Ubuntu

Dùng ứng dụng Disks để xác định đúng thẻ, unmount rồi format FAT32. Không format
theo tên thiết bị đoán mò. Kiểm tra trước bằng:

    lsblk -o NAME,SIZE,MODEL,FSTYPE,MOUNTPOINTS

Chuẩn bị sáu file MP3 nguồn với tên:

    ready.mp3
    attendance_success.mp3
    duplicate.mp3
    time_unavailable.mp3
    enroll_success.mp3
    system_error.mp3

Sau khi Ubuntu tự mount thẻ, chạy:

    cd "$(git rev-parse --show-toplevel)"
    ./tools/prepare_audio_sd.sh /duong-dan/toi/audio-source /media/$USER/TEN_THE
    sync

Kết quả bắt buộc:

    /mp3/0001.mp3
    /mp3/0002.mp3
    /mp3/0003.mp3
    /mp3/0004.mp3
    /mp3/0005.mp3
    /mp3/0006.mp3

Eject thẻ an toàn rồi mới cắm vào DFPlayer. Không hot-plug microSD khi module
đang có nguồn.

## 4. Flash firmware Phase 3

Phase 3 không đổi partition, NVS hay face database. Không chạy erase-flash.

Phase 3 không đổi schema Google Sheet hoặc Apps Script. Heartbeat hiện có tự báo
firmware version mới lên dashboard.

    cd "$(git rev-parse --show-toplevel)/firmware"
    ls -l /dev/serial/by-id/
    eim run "idf.py -p /dev/ttyACM0 flash monitor" v6.0.2

Thay port nếu symlink của board trỏ sang thiết bị khác. Marker mong đợi:

    === Robot Điểm Danh 3.0.0-phase3-dev ===
    Camera OK (VGA, PSRAM)
    P3 audio ready: UART1 TX=GPIO41 RX=GPIO42 enabled=yes volume=18
    DFPlayer responded on UART1
    === Bắt đầu vòng lặp điểm danh ===
    Playing /mp3/0001.mp3

Một số board clone có thể phát được nhưng không trả ACK ổn định. Khi đó dòng
DFPlayer responded hoặc portal có thể báo “chưa phản hồi”; nút phát thử và âm
thanh thực tế là phép kiểm tra cuối cùng.

## 5. Test portal, volume và từng track

1. Khi robot đang chạy, giữ BOOT 2,5 giây; không giữ BOOT trong lúc nhấn RST.
2. Kết nối Wi-Fi RobotSetup.
3. Mở http://192.168.4.1/config.
4. Ở phần Âm thanh DFPlayer, bật âm thanh, đặt volume 10 trước và bấm lưu.
5. Bấm “Phát thử câu điểm danh thành công”.

Để test đủ sáu file khi đang nối RobotSetup:

    for track in 1 2 3 4 5 6; do
      curl -fsS -X POST -d "track=$track" http://192.168.4.1/audio/test
      sleep 3
    done

Tăng volume từ từ. Không bắt đầu ở mức 30 vì loa nhỏ và nguồn USB có thể sụt áp.

Test persistence:

1. đặt volume 12 và bật audio;
2. restart robot, vào lại portal, phải vẫn là 12/enabled;
3. tắt audio, restart, điểm danh phải im lặng nhưng vẫn lưu/upload;
4. bật lại trước full-flow test.

## 6. Full flow hai học sinh

1. Boot normal mode và nghe track 1.
2. Học sinh A điểm danh: đúng một event local/Sheet và track 2.
3. A đứng lại trong cooldown: không có event mới và track 3 không spam quá một
   lần mỗi 5 giây.
4. Học sinh B điểm danh: đúng một event mới và track 2.
5. Mở dashboard, xác nhận device online, firmware 3.0.0-phase3-dev và hai
   attendance đúng session.
6. Vào maintenance, enroll thử một danh tính test nếu cần: khi commit metadata
   thành công phải phát track 5.

## 7. Fault test bắt buộc

### Không có microSD

Tắt nguồn, tháo thẻ, bật lại. DFPlayer có thể không phản hồi/không phát nhưng
camera, nhận diện, lưu local và upload phải tiếp tục.

### Không có DFPlayer

Tắt nguồn, tháo VCC/GND/TX/RX của DFPlayer, bật lại. Firmware vẫn phải vào
recognize loop; không được reset loop hoặc rollback OTA.

### Nguồn yếu/nhiễu

Nếu nghe rè, module reset hoặc ESP báo brownout:

- hạ volume về 8–12;
- rút nguồn trước khi kiểm tra lại GND chung;
- thêm tụ 470–1000 µF sát DFPlayer;
- dùng cáp USB nguồn tốt;
- tách dây loa khỏi dây camera và UART.

### Regression OTA

Giữ DFPlayer cắm, chạy một OTA version mới. Recognition phải dừng đúng lúc OTA,
board boot image mới, qua health gate và audio trở lại. Không đưa DFPlayer vào
OTA health gate.

## 8. Tiêu chí đóng Phase 3

- đủ sáu track đúng mapping;
- volume/mute lưu qua reboot;
- hai học sinh điểm danh và upload đúng một lần;
- duplicate không spam;
- thiếu thẻ/module không ảnh hưởng chức năng cốt lõi;
- không brownout, camera error tăng bất thường hoặc reset loop trong 30 phút.

Lưu monitor log, ảnh dây, ảnh portal audio và ảnh dashboard làm bằng chứng demo
CV.

## Nguồn kỹ thuật

- DFRobot DFPlayer Mini DFR0299: https://wiki.dfrobot.com/dfr0299
- ESP-IDF UART cho ESP32-S3:
  https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/peripherals/uart.html
- ESP32-S3 USB dùng GPIO19/20:
  https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/get-started/establish-serial-connection.html
