# Ghi lời chào theo danh sách lớp trên Ubuntu

Với một danh sách lớp dạng Excel, trước tiên xác nhận tên và năm học còn hiệu
lực. Cột **STT trong Excel không phải Student ID của robot**; mapping âm thanh
luôn dựa trên ID do robot cấp sau khi đăng ký khuôn mặt.

1. Đăng ký khuôn mặt và tên đúng dấu trên robot. Khi nối vào AP `RobotSetup`, mở
   `http://192.168.4.1/` để xem danh sách. Lưu JSON robot làm nguồn ID:

   ```bash
   mkdir -p audio-source
   curl --fail --silent --show-error http://192.168.4.1/students -o audio-source/students.json
   ```

   Giữ `audio-source/` trên máy cá nhân; Git đã bỏ qua thư mục này.

2. Tạo bảng câu đọc. Script chỉ ghép **tên trùng khớp** với robot, báo lỗi nếu
   tên sai dấu/thiếu hoặc ID trùng; không bao giờ lấy STT Excel làm ID:

   ```bash
   python3 tools/plan_audio_from_xlsx.py \
     /duong-dan/danh-sach-lop.xlsx \
     audio-source/students.json > audio-source/recording-plan.csv
   ```

   Mở CSV và soát tên cùng câu đọc với giáo viên. Ví dụ nếu robot báo ID 7 cho
   một học sinh, thu **một file trọn câu** “Xin chào [họ tên], bạn đã điểm danh
   thành công.” với tên `student_0007.mp3`. Chương trình sẽ chép nó thành
   `/mp3/1007.mp3` trên thẻ. Không ghép ba đoạn âm thanh khi phát.

3. Dùng điện thoại hoặc Audacity thu từng câu ở nơi yên tĩnh, rồi xuất MP3
   mono. Giữ âm lượng các file gần nhau. Đặt vào `audio-source/` đúng tên cột
   `File ghi âm` của CSV. Sáu file hệ thống cũng phải có:
   `ready.mp3`, `attendance_success.mp3`, `duplicate.mp3`,
   `time_unavailable.mp3`, `enroll_success.mp3`, `system_error.mp3`.

4. Cắm thẻ microSD và xác định **đúng mountpoint** bằng `lsblk -f`. Sao lưu thẻ
   trước khi chép. Sau đó chạy (thay mountpoint thực tế):

   ```bash
   bash tools/prepare_audio_sd.sh audio-source /media/$USER/TEN_THE audio-source/students.json
   sync
   ```

   Script kiểm tra thiếu file trước khi chép và từ chối thẻ còn lời chào ID cũ
   không thuộc roster hiện tại. Không format hay xóa thẻ chỉ để chạy lại.

5. Lắp thẻ vào DFPlayer, mở portal `/config` → **Âm thanh** → **Kiểm tra lời
   chào theo học sinh**, nhập vài Student ID ở đầu/giữa/cuối danh sách và nghe
   tên có đúng không. Cuối cùng thử điểm danh thật, kiểm tra Sheet và âm thanh.

Khi thêm/chuyển học sinh, lấy lại `students.json`, cập nhật bảng câu đọc, chỉ
thu file cần thay đổi và kiểm tra mapping trước khi chép SD. Nếu Excel là danh
sách cũ, hãy sửa nguồn tên và xác nhận với giáo viên trước; không tự sửa tên
robot hoặc gắn file audio theo thứ tự dòng.
