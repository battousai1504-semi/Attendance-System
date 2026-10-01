# P2.2 teacher dashboard acceptance guide

## 1. Deploy dashboard files

In the staging Apps Script project:

1. Replace `Code.gs` with the repository version.
2. Create an HTML file named exactly `Dashboard`.
3. Copy `google_apps_script/Dashboard.html` into that file.
4. Run `migrateToV2` once after saving.
5. Add Script Property `BOOTSTRAP_DASHBOARD_KEY` with a 12–127 character
   teacher key.
6. Run `provisionDashboardAccessFromProperties`.
7. Confirm the plaintext property was deleted and
   `DASHBOARD_ACCESS_HASH` exists.
8. Update the Web App deployment to a new version.

Open:

```text
<WEB_APP_EXEC_URL>?view=dashboard
```

The normal `/exec` URL must continue returning API health JSON.

## 2. Prepare roster data

Add at least two students to `Students`:

| Student ID | Name | Class | Active | Created At | Updated At |
|---:|---|---|---|---|---|
| 1 | Student A | 12A1 | TRUE | current time | current time |
| 2 | Student B | 12A1 | TRUE | current time | current time |

The IDs must match face IDs on the device. Do not store face images or
embeddings in this sheet.

Optionally add the class to `Classes` for future roster management:

```text
12A1 | Class 12A1 | TRUE | <current time> | <current time>
```

## 3. Create a session in the dashboard

1. Sign in with the dashboard key and an actor name such as `Teacher A`.
2. Expand **Tạo buổi học mới**.
3. Enter a unique Session ID, class, period and `robot-01`.
4. Set start before the current time, late threshold a few minutes later and
   end after the test.
5. Create the session.

Pass criteria:

- a `Sessions` row is appended with status `ACTIVE`;
- an all-numeric ID such as `23092026` remains text and is displayed exactly
  as entered, never as a JavaScript/Google Sheets date;
- `AuditLog` receives `SESSION_CREATE`;
- creating another active session for `robot-01` is rejected;
- the session appears in the class/date/session filters.

## 4. Test attendance calculation

1. Present Student A before the late threshold.
2. Leave Student B without a face event.
3. Refresh the dashboard.

Expected totals:

```text
Sĩ số 2 | Có mặt 1 | Đi muộn 0 | Vắng 1 | Có phép 0
```

For a second session, present Student A after `Late After`. Expected status is
`Đi muộn`. Across every session:

```text
present + late + absent + excused = total roster size
```

## 5. Test audited correction

1. Change Student B from `Vắng` to `Có phép`.
2. Enter an explicit reason.
3. Verify the dashboard totals update.
4. Verify `Attendance` row count and original values did not change.
5. Verify `AuditLog` appended `ATTENDANCE_OVERRIDE` with actor, before JSON,
   after JSON and reason.

Refresh the page. The corrected state must remain because the latest audit
override is reapplied when the dashboard is rebuilt.

## 6. Test filters and export

- filter by class;
- filter by date;
- select each of at least two sessions;
- search by Student ID and name;
- export CSV and open it in LibreOffice with Vietnamese text intact.

The CSV totals and Event IDs must match the selected dashboard session.

## 7. Close the session

Click **Đóng buổi học** and confirm:

- session status becomes `CLOSED`;
- `AuditLog` contains `SESSION_STATUS_CHANGE`;
- the original attendance events remain unchanged;
- a new active session can now be created for the same device.

Create another short session whose `Ends At` is one or two minutes ahead.
Leave the dashboard open. Within one minute the dashboard refresh must show it
as `CLOSED`; with the dashboard closed, the installed trigger must close it
within five minutes. `AuditLog` must contain `SESSION_STATUS_CHANGE`, actor
`SYSTEM`, and reason `Automatically closed at Ends At`.

## 8. Security checks

- wrong dashboard key returns `dashboard_unauthorized`;
- access key never appears in the browser URL;
- closing the browser tab removes the key with `sessionStorage`;
- `Devices` contains only secret hashes;
- the public health endpoint exposes no token, hash, student roster or audit
  details.
