# Attendance API v2 contract

Status: design frozen for Phase 2 implementation
Contract version: `2`
Transport: HTTPS JSON through the deployed Google Apps Script `/exec` URL

## 1. Compatibility rules

- Every new request carries `api_version: 2` and `type`.
- During migration, a payload without `type` is treated as `attendance` so the
  Phase 1 firmware continues to upload its durable outbox.
- The Phase 1 nine-column `Attendance` prefix is never reordered or deleted.
  Migration only appends v2 columns.
- Unknown fields are ignored. Missing required fields are rejected.
- Every response includes `ok`, `api_version` and `retryable`.
- HTTP transport success is not enough to acknowledge an outbox record. The
  firmware removes it only when the JSON has `ok: true` and the matching
  `event_id`.

## 2. Authentication

All device requests contain:

```json
{
  "device_id": "robot-01",
  "token": "device-secret-with-at-least-32-characters"
}
```

Phase 2 accepts the existing Script Property `API_TOKEN` only while
`ALLOW_LEGACY_TOKEN=true` and the device does not yet have a `Devices` row.
The first accepted request creates that row with a SHA-256 hash. A provisioned
device always uses its own hash and a disabled device is rejected even if its
secret is valid. Plaintext tokens and Wi-Fi credentials must never appear in
response bodies, logs, Git or spreadsheet exports.

## 3. Attendance request

```json
{
  "api_version": 2,
  "type": "attendance",
  "event_id": "1789490393-1-9fb85e40",
  "device_id": "robot-01",
  "token": "...",
  "student_id": 1,
  "name": "Pham Le Hai Dang",
  "class": "12A1",
  "timestamp": "2026-09-15 23:39:53",
  "confidence_milli": 652,
  "method": "FACE",
  "session_id": "12A1-20260915-P1",
  "firmware_version": "2.5.0-phase2-dev"
}
```

`session_id` may be empty while the device has not synchronized an active
session. The server may resolve it from class, device and timestamp. Event ID
is the idempotency key and must never be reused for different event contents.

Accepted response:

```json
{
  "ok": true,
  "api_version": 2,
  "retryable": false,
  "event_id": "1789490393-1-9fb85e40",
  "duplicate": false,
  "session_id": "12A1-20260915-P1"
}
```

A duplicate returns the same shape with `duplicate: true` and does not append
a second row.

## 4. Heartbeat request

Sent at boot after networking becomes ready and then every 60 seconds. It must
run in the core-1 background uploader while recognition remains on core 0, and
must not block camera processing.

```json
{
  "api_version": 2,
  "type": "heartbeat",
  "device_id": "robot-01",
  "token": "...",
  "firmware_version": "2.5.0-phase2-dev",
  "uptime_seconds": 382,
  "rssi": -56,
  "free_heap": 184320,
  "free_psram": 6127616,
  "pending_events": 2,
  "sync_latency_ms": 842,
  "consecutive_failures": 0,
  "last_error": ""
}
```

Response:

```json
{
  "ok": true,
  "api_version": 2,
  "retryable": false,
  "device_id": "robot-01",
  "server_time": "2026-09-16T09:00:00.000Z"
}
```

## 5. Session synchronization

Request:

```json
{
  "api_version": 2,
  "type": "session_sync",
  "device_id": "robot-01",
  "token": "...",
  "class": "12A1"
}
```

Response when a session is active:

```json
{
  "ok": true,
  "api_version": 2,
  "retryable": false,
  "active": true,
  "session": {
    "session_id": "12A1-20260916-P1",
    "class": "12A1",
    "starts_at": "2026-09-16T07:00:00+07:00",
    "late_after": "2026-09-16T07:10:00+07:00",
    "ends_at": "2026-09-16T07:45:00+07:00"
  }
}
```

No active session is a successful state: `ok: true`, `active: false`.

## 6. OTA manifest

Request được gửi sau 60 giây từ boot và lặp mỗi sáu giờ:

```json
{
  "api_version": 2,
  "type": "ota_manifest",
  "device_id": "robot-01",
  "token": "...",
  "firmware_version": "2.5.0-phase2-dev"
}
```

Khi phát hành đã bật và target khác current, response chứa `version`, HTTPS
`url`, `size` (tối đa `0x600000`) và SHA-256 hex của toàn bộ file `.bin`.
`OTA_ENABLED=false` hoặc target trùng current trả `available:false`. URL chỉ
được gửi sau device authentication; binary vẫn phải được host dưới HTTPS.
Firmware kiểm tra version nhúng trong app image, kích thước thực và SHA-256
trước khi gọi bước chọn boot partition.

## 7. Error model

| Error | Retryable | Meaning |
|---|---:|---|
| `invalid_json` | no | Body cannot be parsed |
| `invalid_body` | no | JSON root is not an object |
| `unsupported_api_version` | no | Device contract is not supported |
| `unknown_request_type` | no | `type` is not recognized |
| `unauthorized` | no | Secret does not match |
| `device_disabled` | no | Device was administratively disabled |
| `invalid_schema` | no | Required field/type/range is wrong |
| `event_conflict` | no | Event ID exists with different identity data |
| `invalid_session` | no | Supplied session does not match class/device/time |
| `session_conflict` | no | More than one session matches the request |
| `ota_manifest_invalid` | no | OTA is enabled but version/URL/size/hash is invalid |
| `busy` | yes | Apps Script lock was unavailable |
| `internal_error` | yes | Unexpected server/storage failure |

Non-retryable events stay visible in the device diagnostics instead of being
retried aggressively or silently deleted. All cloud failures use exponential
backoff from 5 seconds with ±25% jitter, capped at 300 seconds. A successful
recovery heartbeat reports the accumulated failure count and last error before
clearing them locally.

## 8. Google Sheets schema v2

### Attendance (append-only)

`Received At`, `Event ID`, `Device ID`, `Student ID`, `Name`, `Class`,
`Device Timestamp`, `Confidence (milli)`, `Method`, `Session ID`,
`Firmware Version`, `API Version`.

### Students

`Student ID`, `Name`, `Class`, `Active`, `Created At`, `Updated At`.

### Classes

`Class ID`, `Display Name`, `Active`, `Created At`, `Updated At`.

### Sessions

`Session ID`, `Class ID`, `Date`, `Period`, `Starts At`, `Late After`,
`Ends At`, `Status`, `Device ID`, `Created At`.

Valid status values: `DRAFT`, `ACTIVE`, `CLOSED`, `CANCELLED`. A device may
have at most one `ACTIVE` session.

`Session ID` is an opaque text identifier even when it contains only digits;
Google Sheets date/number coercion must not change it. An `ACTIVE` session is
automatically moved to `CLOSED` after `Ends At` by dashboard/API access and by
the five-minute `closeExpiredSessions` trigger. Automatic transitions append
a `SESSION_STATUS_CHANGE` audit entry with actor `SYSTEM`.

### Devices

`Device ID`, `Enabled`, `Secret Hash`, `Last Seen`, `Firmware Version`, `RSSI`,
`Free Heap`, `Free PSRAM`, `Pending Events`, `Uptime (s)`, `Last Error`,
`Updated At`, `Secret Rotated At`, `Status`, `Sync Latency (ms)`,
`Consecutive Failures`.

`Status` is server-derived: enabled devices are `ONLINE` for 180 seconds
after `Last Seen`, then become `OFFLINE`; disabled rows are `DISABLED`.

### AuditLog (append-only)

`Timestamp`, `Actor`, `Action`, `Entity Type`, `Entity ID`, `Before JSON`,
`After JSON`, `Reason`.

## 9. Migration invariants

1. Acquire the script lock.
2. Validate the existing nine `Attendance` headers exactly.
3. Append only the three v2 headers; never rewrite attendance rows.
4. Create missing v2 sheets and headers.
5. Record schema version `2` in Script Properties only after every sheet is
   valid.
6. Running migration repeatedly is a no-op.
7. On any mismatch, stop and report the sheet/header; do not auto-delete or
   rename user data.

## 10. Acceptance tests

- migration from an empty spreadsheet;
- migration from the Phase 1 nine-column sheet;
- migration run twice gives the same schema and data;
- altered Phase 1 header fails without mutation;
- legacy attendance payload remains accepted;
- v2 attendance succeeds and duplicate is idempotent;
- wrong token, disabled device and bad schema do not mutate sheets;
- heartbeat updates one device row rather than appending duplicates;
- device transitions ONLINE/OFFLINE/DISABLED from server time;
- CRC32 detects bit flips/truncation while legacy outbox v1 remains readable;
- backoff stays within its jitter bounds and 300-second cap;
- one device cannot resolve two simultaneous active sessions;
- server-busy response is marked retryable.
