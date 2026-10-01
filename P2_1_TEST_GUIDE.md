# P2.1 staging and hardware acceptance guide

This guide is retained as the P2.1 milestone record. Current P2.3 firmware no
longer reads compile-time secret headers; use
`P2_2_P2_3_FULL_FLOW_TEST_GUIDE.md` for the current end-to-end procedure.

Use a staging spreadsheet first. Do not overwrite the working Phase 1 Web App
deployment until every check below passes.

## 1. Prepare the staging Apps Script

1. Make a Google Sheets copy named `Attendance System - Phase 2 Staging`.
2. Open **Extensions → Apps Script**.
3. Replace `Code.gs` with `google_apps_script/Code.gs` from this repository.
4. Confirm `appsscript.json` uses V8 and timezone `Asia/Ho_Chi_Minh`.
5. In Script Properties set:
   - `SPREADSHEET_ID`: staging spreadsheet ID;
   - `API_TOKEN`: existing migration token, at least 32 characters.
6. Run `setup`.
7. Run `migrateToV2` twice. Both runs must succeed and the second must not add
   columns, sheets or rows.

Expected sheets:

```text
Attendance  Students  Classes  Sessions  Devices  AuditLog
```

The health endpoint is not ready until migration has completed.

## 2. Provision `robot-01`

In Script Properties add:

```text
BOOTSTRAP_DEVICE_ID=robot-01
BOOTSTRAP_DEVICE_SECRET=<same 32-127 character value as CDP_GAS_TOKEN>
```

Run `provisionDeviceFromProperties` once. Pass criteria:

- result has `ok: true` and `device_id: robot-01`;
- `Devices` contains one enabled `robot-01` row;
- `Secret Hash` contains 64 hexadecimal characters, not the plaintext secret;
- `BOOTSTRAP_DEVICE_SECRET` is automatically removed from Script Properties.

Keep `ALLOW_LEGACY_TOKEN=true` until the old outbox has drained. Set it to
`false` only after every device has a dedicated row and secret.

## 3. Create a test session

Add one `Sessions` row. Use actual Google Sheets date/time cell values, not
formula text:

| Column | Example |
|---|---|
| Session ID | `12A1-20260921-P1` |
| Class ID | `12A1` |
| Date | `2026-09-21` |
| Period | `P1` |
| Starts At | five minutes before the test |
| Late After | five minutes after the test |
| Ends At | one hour after the test |
| Status | `ACTIVE` |
| Device ID | `robot-01` |
| Created At | current date/time |

There must be at most one `ACTIVE` session matching a device and class.

## 4. Deploy the staging Web App

1. Select **Deploy → New deployment → Web app**.
2. Execute as the project owner.
3. Allow access to **Anyone**.
4. Deploy and copy the new `/exec` URL.
5. Open the URL in a browser.

Expected response fields:

```json
{
  "ok": true,
  "service": "robot-attendance",
  "api_version": 2,
  "schema_version": "2",
  "configured": true,
  "sheet_ready": true
}
```

Do not use an editor `/dev` URL and do not reuse the old deployment URL unless
that deployment was explicitly updated to the new version.

## 5. Configure and flash firmware

In the ignored local `firmware/main/cloud_config.h`, set:

- `CDP_GAS_URL` to the staging `/exec` URL;
- `CDP_GAS_TOKEN` to the provisioned device secret;
- `CDP_DEVICE_ID` to `robot-01`.

Then run:

```bash
cd ~/VScode/Projects/Attendance-System/firmware
source ~/.espressif/tools/activate_idf_v6.0.2.sh
idf.py -p /dev/ttyACM0 flash monitor
```

Do not run `erase-flash`; the unchanged outbox record layout is intentional.

Expected boot marker:

```text
Robot Điểm Danh 2.0.0-phase2-dev
```

## 6. End-to-end acceptance

1. Wait for Wi-Fi and NTP sync.
2. Present an enrolled student once.
3. Record the Event ID from serial.
4. Verify exactly one `Attendance` row contains:
   - the same Event ID;
   - expected Device ID and Student ID;
   - the active Session ID;
   - firmware version `2.0.0-phase2-dev`;
   - API Version `2`.
5. Present the same student inside cooldown; no second row may appear.
6. Reboot with one pending/offline event, reconnect, and verify it appears once.
7. Confirm the `Devices` row updates `Last Seen` and firmware version.

The firmware does not send periodic heartbeat until P2.4. The P2.1 heartbeat
route is covered by contract tests and can be exercised independently; normal
attendance requests already update device last-seen/version.

## 7. Failure checks

- Set `Enabled` to `FALSE`: the server must return `device_disabled` and append
  no attendance row.
- Use a wrong secret: the server must return `unauthorized`.
- Create two matching active sessions: `session_sync` must return
  `session_conflict`; remove the extra session immediately.
- Reuse an Event ID with another student/timestamp: the server must return
  `event_conflict` and preserve the original row.

Restore `Enabled=TRUE` and a single active session after testing.
