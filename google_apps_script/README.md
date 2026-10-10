# Google Apps Script backend

Backend API v2 nhận `attendance`, `heartbeat`, `session_sync` và
`ota_manifest` từ ESP32.
`event_id` là khóa idempotency nên firmware có thể retry mà không tạo dòng
trùng. Payload Phase 1 không có `type` vẫn được hiểu là `attendance` trong giai
đoạn chuyển đổi.

## Thiết lập

1. Tạo một Google Spreadsheet và một Apps Script project.
2. Chép `Code.gs` và `appsscript.json` vào project.
3. Trong **Project Settings → Script Properties**, thêm:
   - `SPREADSHEET_ID`: ID của spreadsheet (có thể bỏ qua nếu script được bind vào sheet).
   - `API_TOKEN`: token migration legacy nếu còn thiết bị Phase 1; thiết bị
     P2.3 dùng secret riêng được provision ở bước 5.
4. Chạy `setup`, sau đó chạy `migrateToV2` một lần và cấp quyền.
5. Chạy `installPhase2Automation` một lần để tạo trigger tự đóng session mỗi
   năm phút và trigger cập nhật online/offline mỗi phút.
6. Provision secret riêng cho thiết bị theo mục bên dưới.
7. Deploy dưới dạng **Web app**, execute as owner, chọn **Anyone** để ESP32
   truy cập mà không cần đăng nhập Google.
8. Nhập URL deployment và device secret vào maintenance portal P2.3 tại
   `http://192.168.4.1/config`.

Mỗi lần sửa Apps Script cần tạo deployment version mới hoặc cập nhật deployment
hiện tại. Không đưa `API_TOKEN` vào source control.

Dashboard dành cho giáo viên đặt buổi đang hoạt động và máy điểm danh ở đầu trang;
buổi cũ nằm trong mục lịch sử. Giao diện tiểu học dùng **Buổi sáng 07:00–10:30**
và **Buổi chiều 14:00–16:30** (giờ Việt Nam) thay cho tiết học. Backend vẫn lưu
nhãn đó trong cột `Period` hiện có, nên không cần đổi schema Sheet. Mốc đi
muộn mặc định sau 10 phút. Nút một chạm chỉ gợi ý lớp có buổi gần đây **cùng
Sáng/Chiều**, máy ONLINE và chưa có buổi tương ứng của lớp trong ngày; đây
chưa phải thời khóa biểu tự động. **Bắt đầu buổi học mới** chỉ cần chọn lớp
đang cấu hình và Sáng/Chiều; một máy được chọn sẵn, nhiều máy thì giáo viên
chọn máy ngay trong biểu mẫu. Không có lớp/máy online thì không thể bấm bắt
đầu. Giờ và mã buổi có thể đổi trong **Tùy chọn nâng cao**. Lọc lịch sử và
bảng chẩn đoán thiết bị nằm trong các mục thu gọn. Thay cả `Code.gs` và
`Dashboard.html` rồi cập nhật Web App deployment; không đổi schema Sheet.

## OTA P2.5

Apps Script chỉ phân phối manifest đã xác thực; file `.bin` được host tại URL
HTTPS trực tiếp. Mặc định an toàn là `OTA_ENABLED=false`. Khi đã upload binary,
dùng `tools/package_ota.sh` để lấy và đặt các Script Properties:

- `OTA_VERSION`: version nhúng trong candidate, tối đa 31 ký tự;
- `OTA_URL`: URL HTTPS trả raw binary;
- `OTA_SIZE`: kích thước byte, không quá `6291456`;
- `OTA_SHA256`: 64 ký tự hex của toàn file;
- `OTA_ENABLED=true` chỉ trong cửa sổ phát hành/test.

Property sai định dạng trả `ota_manifest_invalid`; target trùng version đang
chạy hoặc OTA tắt trả `available:false`. Thay Script Properties không cần tạo
deployment mới. Quy trình checksum, power-cut và rollback nằm trong
`../P2_5_TEST_GUIDE.md`.

## Chuẩn bị schema Phase 2 trên sheet staging

Không chạy migration lần đầu trên sheet đang nhận dữ liệu thật. Tạo một
spreadsheet staging, đặt ID của nó vào Script Property `SPREADSHEET_ID`, rồi:

1. Chép phiên bản mới nhất của `Code.gs` vào Apps Script.
2. Chọn hàm `setup`, bấm **Chạy** và cấp quyền nếu project còn mới.
3. Chọn hàm `migrateToV2`, bấm **Chạy**.
4. Kiểm tra log trả về `schema_version: "2"`, `repaired_session_ids`,
   `repaired_attendance_session_ids` và đủ sáu sheet:
   `Attendance`, `Students`, `Classes`, `Sessions`, `Devices`, `AuditLog`.
5. Chạy `installPhase2Automation`; trong **Triggers** phải có đúng một
   `closeExpiredSessions` chạy mỗi năm phút và một
   `refreshDeviceStatuses` chạy mỗi phút.
6. Chạy lại `migrateToV2`. Số sheet, số cột và dữ liệu phải giữ nguyên.

Migration giữ nguyên chín cột Phase 1 của `Attendance`, chỉ nối thêm `Session
ID`, `Firmware Version`, `API Version`. Với sheet `Devices` 12 hoặc 13 cột
từ bản cũ, migration nối thêm `Secret Rotated At`, `Status`,
`Sync Latency (ms)` và `Consecutive Failures` mà không sửa dữ liệu cũ. Nếu header hiện có không
đúng, hàm dừng với `Schema mismatch` và không tự đổi tên/xóa dữ liệu. Hợp đồng
payload đầy đủ nằm trong `../API_V2_CONTRACT.md`.

`Session ID` được ép lưu ở dạng text. Migration sửa các ID toàn số đã bị
Google Sheets đổi thành ngày (ví dụ ngày 23/09/2026 được phục hồi thành
`23092026`) và dừng nếu việc phục hồi tạo ID trùng.

## Provision hoặc xoay secret cho thiết bị

Không ghi secret plaintext vào sheet `Devices`. Trong **Project Settings →
Script Properties**:

1. Thêm `BOOTSTRAP_DEVICE_ID`, ví dụ `robot-01`.
2. Thêm `BOOTSTRAP_DEVICE_SECRET`, dài 32–127 ký tự. Nên dùng hex ngẫu nhiên
   để dễ nhập chính xác tại portal thiết bị.
3. Chạy hàm `provisionDeviceFromProperties`.
4. Xác nhận sheet `Devices` có device, `Enabled = TRUE` và chỉ có `Secret
   Hash`. Hàm tự xóa property `BOOTSTRAP_DEVICE_SECRET` sau khi thành công.

Chạy lại quy trình với secret mới để rotate key, sau đó mở maintenance portal
và lưu secret mới. Portal gửi heartbeat xác thực trước khi đổi cấu hình active;
nếu secret sai, cấu hình cũ không bị thay thế.

Trong thời gian nâng cấp, `ALLOW_LEGACY_TOKEN=true` cho phép một device chưa có
row sử dụng `API_TOKEN`. Request hợp lệ đầu tiên tự tạo row với hash của token.
Sau khi mọi device đã được provision riêng, đổi property này thành `false`.

## Dashboard giáo viên

Trong Apps Script, tạo thêm một HTML file tên chính xác `Dashboard` rồi chép
toàn bộ nội dung `Dashboard.html` từ repository. Không đổi tên file vì
`doGet()` tải đúng tên này.

Tạo tài khoản giáo viên trong Apps Script (không chép mật khẩu vào source):

1. Trong **Project Settings → Script Properties**, thêm
   `BOOTSTRAP_TEACHER_USERNAME` = tài khoản giáo viên đã thống nhất và
   `BOOTSTRAP_TEACHER_PASSWORD` = mật khẩu chủ dự án cấp.
2. Chọn hàm `provisionDashboardAccountFromProperties`, bấm **Run** một lần và
   cấp quyền nếu Apps Script yêu cầu.
3. Xác nhận hai property `BOOTSTRAP_...` đã bị xóa. Chỉ còn
   `DASHBOARD_USERNAME`, `DASHBOARD_PASSWORD_SALT`,
   `DASHBOARD_PASSWORD_HASH` và `DASHBOARD_CREDENTIAL_VERSION`.
4. Update Web App deployment sang version mới.
5. Mở `<WEB_APP_EXEC_URL>?view=dashboard`.

Web App vẫn để **Anyone** để ESP32 gọi API, nhưng mọi hàm đọc/sửa dữ liệu
dashboard đều kiểm tra phiên đăng nhập từ server. Mật khẩu không lưu trong
trình duyệt; chỉ token phiên tối đa 6 giờ ở `sessionStorage`. Đăng xuất xóa
token. CacheService có thể hết hạn sớm, khi đó đăng nhập lại. Sau 5 lần sai,
tài khoản bị giới hạn đăng nhập 15 phút. AuditLog lấy tên giáo viên từ phiên,
không tin tên tự gửi từ trình duyệt. Đổi mật khẩu: đặt lại hai property
`BOOTSTRAP_...` rồi chạy lại hàm provisioning.

Trước khi dùng dashboard, điền roster vào `Students`:

```text
Student ID | Name | Class | Active | Created At | Updated At
```

`Active` phải là checkbox/boolean `TRUE`. Dashboard hỗ trợ:

- tạo, chọn và đóng buổi học;
- lọc theo lớp, ngày, session và học sinh;
- tổng hợp Có mặt, Đi muộn, Vắng, Có phép;
- xuất CSV UTF-8 có cột `Session ID` và `Reason` (lý do điều chỉnh cuối cùng,
  để trống nếu chưa điều chỉnh);
- manual correction có người sửa và lý do trong `AuditLog`;
- bảng device health với ONLINE/OFFLINE, RSSI, heap, PSRAM, pending, latency và
  lỗi gần nhất;
- giữ nguyên mọi dòng sự kiện gốc trong `Attendance`.

Khi chuyển lại một buổi đã đóng từng xem trong cùng phiên trình duyệt, dashboard
hiện bản tạm ngay rồi làm mới từ server. Trong lúc đồng bộ, nút sửa/đóng/xuất
CSV bị khóa để tránh dùng dữ liệu cũ; nếu làm mới thất bại, bấm **Làm mới**.
Sau khi thay `Code.gs` và `Dashboard.html`, cập nhật **deployment hiện tại**
lên version mới và tải lại trang. Kiểm tra chuyển nhanh A → B → A không hiển
thị nhầm buổi, rồi xuất CSV để đối chiếu cột `Session ID` và `Reason`. Bản UX
2026-10-02 thay **cả `Code.gs` và `Dashboard.html`** rồi cập nhật deployment:
backend trả danh sách lớp từ `Classes` (hoặc roster `Students` nếu chưa cấu
hình lớp) và số lượt Attendance nhận hôm nay chưa gắn vào session. Dashboard
cảnh báo số lượt đó, không tự cộng chúng vào sĩ số hay tự gán lại vào buổi.
Làm mới tự động mỗi 30 giây khi trang đang hiện; ở mạng chậm giáo viên có thể
bấm **Làm mới danh sách**. Không cần chạy lại migration hoặc tạo Sheet mới chỉ
để áp dụng thay đổi UX này.

Session `ACTIVE` được chuyển thành `CLOSED` khi `Ends At` đã qua: dashboard/API
thực hiện ngay khi có request, còn time-driven trigger xử lý tối đa trong năm
phút ngay cả khi không ai mở dashboard. Mọi lần tự đóng đều ghi
`SESSION_STATUS_CHANGE` với actor `SYSTEM` vào `AuditLog`. Device được tính
`OFFLINE` khi quá 180 giây không có heartbeat; trigger health chạy mỗi phút
nên dashboard không phụ thuộc việc giáo viên đang mở trang.

Mở URL `/exec` trong trình duyệt để kiểm tra deployment. API v2 phải trả JSON
chứa:

```json
{"ok":true,"service":"robot-attendance","api_version":2,"schema_version":"2","configured":true,"sheet_ready":true}
```

Google Content Service trả kết quả qua redirect tới `script.googleusercontent.com`.
Client phải theo redirect; với phản hồi `301`, `302` hoặc `303` sau POST, request
kế tiếp dùng GET. Firmware đã xử lý quy tắc này trong `http_upload.c`.
