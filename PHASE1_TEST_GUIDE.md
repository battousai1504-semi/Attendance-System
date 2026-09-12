# Phase 1 complete test guide

This guide verifies the ESP32-S3 camera, enrollment, recognition, local
attendance storage, duplicate cooldown, Wi-Fi/NTP, Google Sheets upload, and
offline retry behavior.

## 1. Prepare the Google Apps Script backend

1. Open the target Google Sheet and its Apps Script project.
2. Ensure `Code.gs` and `appsscript.json` match the files in
   `google_apps_script/`.
3. In **Project Settings > Script Properties**, configure:
   - `SPREADSHEET_ID`: the target spreadsheet ID.
   - `API_TOKEN`: exactly the same token used by `firmware/main/cloud_config.h`.
4. Run the Apps Script function `setup()` once and grant permissions.
5. Deploy as **Web app**:
   - Execute as: **Me**.
   - Who has access: a setting that permits the ESP32 to call the URL.
6. After editing the script, update the existing deployment to a new version.
7. Open the `/exec` deployment URL in a browser. Expected response:

```json
{"ok":true,"service":"robot-attendance"}
```

## 2. Verify local configuration

Check these local-only files without publishing their contents:

- `firmware/main/wifi_time_config.h`: correct Wi-Fi SSID and password.
- `firmware/main/cloud_config.h`: `/exec` deployment URL, matching API token,
  and device ID.

## 3. Build, flash, and monitor

Open PowerShell and run:

```powershell
. 'C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1'
cd 'C:\Users\ACER\.gemini\antigravity\scratch\robot-attendance\firmware'
idf.py build
[System.IO.Ports.SerialPort]::GetPortNames()
idf.py -p COMx flash monitor
```

Replace `COMx` with the detected port, for example `COM7`. Exit the monitor
with `Ctrl+]`.

Do not run `erase-flash` during an ordinary update. A normal flash preserves
enrolled faces, attendance history, and pending uploads.

Expected boot markers:

```text
Robot Điểm Danh 1.2-phase1
Camera OK (VGA, PSRAM)
student_db ready
Face engine initialized
Attendance storage ready: /spiffs/attendance.csv
Verified TLS CA store ready
Durable uploader ready
```

## 4. Test enrollment

Enrollment mode starts when the face database is empty or when the previous
enrollment session was not explicitly completed. Therefore, a power loss in
the middle of registering a class does not lose the session. A firmware update
from an older version may open enrollment once so the teacher can review the
existing list and press **Kết thúc buổi đăng ký**.

Only use `idf.py -p COMx erase-flash` when a clean factory reset is truly
required. It permanently deletes enrolled faces, attendance history, and the
upload outbox.

For a clean device:

1. Wait for the log that announces enrollment mode.
2. Connect a phone or laptop to the enrollment Wi-Fi shown in the log.
3. Open `http://192.168.4.1`.
4. Enter the class once, then enter the first student's name.
5. Place exactly one face in good, even light, about 40-80 cm from the camera.
6. Press **Chụp & Đăng ký** and wait for a returned face ID.
7. Verify the count and student list update. The class remains filled while
   the name field is cleared and focused for the next student.
8. Register students one by one. Use **Hoàn tác em cuối** to test removal of
   the latest face and metadata, then enroll that student again.
9. Before finishing, reboot once and verify the AP reopens with the previously
   saved list. Then press **Kết thúc buổi đăng ký** only after the class is
   complete.

Pass criteria:

```text
Enrolled: id=... name='...' class='...'
Đã đăng ký ... khuôn mặt. Chuyển sang chế độ điểm danh.
```

Negative checks:

- No face: web page reports that no face was detected.
- Multiple faces: web page asks for only one face.
- Empty or overlong name: web page rejects the data.

## 5. Test Wi-Fi and NTP

After leaving enrollment mode, expect:

```text
Connecting to configured Wi-Fi network
Wi-Fi connected; waiting for NTP time
NTP synchronization started (time.google.com, pool.ntp.org, time.cloudflare.com)
NTP time synchronized
```

Pass criteria: the device timestamp is local Vietnam time and is later than
2024. If NTP is not ready, attendance must be rejected with:

```text
Attendance ignored: waiting for Wi-Fi/NTP clock
```

## 6. Test recognition and local attendance

1. Present an enrolled face under similar lighting to enrollment.
2. Keep only one main face in view.
3. Wait for a successful match.

Pass criteria:

```text
ATTENDANCE SAVED: YYYY-MM-DD HH:MM:SS | ID=... | FACE | event=...
ĐIỂM DANH: <name> (<class>) conf=...
```

The status LED blinks twice quickly for a new attendance record.

## 7. Test duplicate cooldown

Present the same face again within 60 seconds.

Pass criteria:

```text
Duplicate ignored: ID=... (FACE)
Duplicate: <name> (cooldown)
```

Only one LED blink is expected and no second Google Sheet row should be
created. After waiting more than 60 seconds, the same face may create a new
attendance event.

## 8. Test Google Sheets upload and TLS

After a new attendance event, expect:

```text
Uploaded event=... student_id=...
```

The Google Sheet `Attendance` tab must contain one row whose Event ID matches
the serial log. Verify Device ID, Student ID, Name, Class, timestamp,
confidence, and method `FACE`.

The following messages mean the test failed:

```text
mbedtls_ssl_handshake returned -0x2700
mbedtls_x509_crt_parse of CA cert returned -0x008D
No matching trusted root certificate found
Upload failed
```

## 9. Test durable offline retry

1. Turn off the configured Wi-Fi access point.
2. Present an enrolled face and confirm `ATTENDANCE SAVED` appears locally.
3. Confirm upload fails but the device continues running.
4. Reboot the ESP32 while Wi-Fi is still unavailable.
5. Turn Wi-Fi back on and wait up to 30 seconds after connection/NTP sync.

Pass criteria:

- Boot log reports one or more pending records.
- Each pending event is eventually logged as `Uploaded event=...`.
- Each Event ID appears only once in Google Sheets, even after retries/reboots.

## Phase 1 acceptance checklist

- [ ] Firmware `1.2-phase1` boots without panic or restart loop.
- [ ] Camera and PSRAM initialize.
- [ ] Batch enrollment shows progress, supports undo, and resumes after reboot.
- [ ] Enrollment rejects invalid scenes and saves a valid student.
- [ ] Enrolled face and student metadata survive a normal reboot.
- [ ] Wi-Fi reconnects and NTP provides correct Vietnam time.
- [ ] Recognition creates a local attendance event.
- [ ] Duplicate attendance is blocked for 60 seconds.
- [ ] HTTPS upload succeeds without TLS `-0x2700`.
- [ ] Sheet data matches the device log.
- [ ] Offline events survive reboot and upload later without duplicate rows.
