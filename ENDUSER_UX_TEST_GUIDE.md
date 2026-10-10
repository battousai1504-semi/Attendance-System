# Kiểm thử UX giáo viên tiểu học — bản source 2026-10-08

## Trạng thái triển khai

Automated tests PASS ở local. Chưa xác nhận Apps Script Web App hiện tại đã
deploy bản `Code.gs` + `Dashboard.html` mới; chưa nghiệm thu giao diện trên
điện thoại và robot thật. Không sửa trực tiếp Sheet đang dùng trong lúc có
buổi học thật. Nên thử trên staging hoặc vào giờ không có lớp.

## Chuẩn bị

1. Lưu cả hai file `google_apps_script/Code.gs` và
   `google_apps_script/Dashboard.html` vào project Apps Script hiện tại. File
   HTML trong Apps Script phải tên `Dashboard`.
2. Cập nhật **deployment Web App hiện tại** sang version mới, giữ nguyên URL
   `/exec`, quyền và Script Properties. Không chạy lại `setup`, `migrateToV2`
   hoặc `installPhase2Automation` chỉ vì thay giao diện.
3. Mở `?view=dashboard`, đăng nhập bằng tài khoản giáo viên. Không gửi mật khẩu, URL riêng
   hoặc danh sách học sinh lên Git.

## Ca kiểm thử

| Ca | Hành động | Kết quả cần thấy |
|---|---|---|
| UX-01 | Có buổi ACTIVE, mở dashboard | Đầu trang ghim đúng lớp/buổi Sáng hoặc Chiều, trạng thái Đang điểm danh và máy online/offline. Không mời bắt đầu buổi khác như hành động chính. |
| UX-02 | Xem một buổi CLOSED trong mục lịch sử khi buổi khác ACTIVE | Đầu trang vẫn chỉ đúng buổi ACTIVE; có nút Xem buổi đang diễn ra. CSV ghi rõ là của buổi đang xem. |
| UX-03 | Không có buổi ACTIVE, máy ONLINE, có buổi CLOSED gần đây **cùng Sáng/Chiều** | Nút ghi rõ buổi và lớp. Bấm một lần tạo mã mới với giờ hôm nay: Sáng 07:00–10:30 hoặc Chiều 14:00–16:30, đi muộn sau 10 phút. Không lấy nhầm tiết P1/P2 lịch sử. |
| UX-04 | Máy OFFLINE, đã qua giờ kết thúc, hoặc lớp đã có buổi cùng Sáng/Chiều hôm nay | Không gợi ý một chạm; biểu mẫu báo rõ tình trạng. Không tạo trùng do bấm nhầm. |
| UX-05 | Tạo buổi bằng biểu mẫu nhanh | Chỉ chọn **lớp đang được cấu hình** và Sáng/Chiều. Một máy được tự chọn; nếu có nhiều máy, giáo viên chọn máy. Kiểm tra giờ bắt đầu/kết thúc và mốc đi muộn trên Sheet. |
| UX-06 | Xem danh sách, sửa một ngoại lệ có lý do, xuất CSV | Phản hồi thành công, tải lại không mất sửa, AuditLog và cột Reason/Session ID đúng. |
| UX-07 | Một event được backend nhận mà không gắn session | Đầu trang cảnh báo số lượt chưa gắn buổi; lượt này không bị tính vào sĩ số. Không đưa dữ liệu thật vào ca test nếu không dùng staging. |
| UX-08 | Mở trên điện thoại | Chữ, nút đủ lớn; danh sách vẫn thấy tên, trạng thái và nút Điều chỉnh mà không phải kéo ngang. Các cột kỹ thuật chỉ hiện khi mở mục riêng. |
| UX-09 | Mở buổi Chiều trước 14:00 | Đầu trang ghi **Sắp bắt đầu 14:00**, không nói “Đang điểm danh” khi chưa đến giờ. |
| UX-10 | Đã qua giờ kết thúc nhưng trigger chưa kịp đóng | Đầu trang ghi **Đã hết giờ, đang chờ đóng**; sau trigger hoặc thao tác đóng thủ công, trạng thái chuyển sang Đã kết thúc. |

Mở trên điện thoại, kiểm tra phần đầu trang, nút gợi ý, danh sách và hộp điều
chỉnh không bị tràn ngang. Thử chuyển nhanh hai buổi rồi quay lại; không xuất
CSV khi dữ liệu đang đồng bộ hoặc báo cũ.

## Chưa thuộc bản này

- Nút một chạm là gợi ý từ **buổi gần nhất cùng Sáng/Chiều**, không phải thời khóa biểu có
  ngày nghỉ/dạy thay. Lịch dạy cần thiết kế quyền chỉnh sửa và nguồn dữ liệu
  trước khi tự động gợi ý theo tuần.
- Robot chưa biết session ACTIVE tại thời điểm ghi local, nên câu nói thành
  công chưa phải xác nhận sự kiện đã được Sheet gắn vào buổi. Cảnh báo
  `unassigned_today` là bước phát hiện, chưa phải sửa giao thức. Khi triển
  khai session-aware audio cần thêm giao thức đồng bộ, xử lý offline và test
  lại cả loa/firmware; không được lấy kết quả PASS loa cũ để chốt thay đổi đó.
