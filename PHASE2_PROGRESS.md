# Phase 2 progress

Last updated: 2026-09-25

## P2.0 — Phase 1 baseline

Source work completed:

- Phase 1 end-to-end attendance passed on the target board with two students;
- firmware baseline version was promoted from release candidate;
- unsafe GPIO0 sampling at reset was replaced by runtime BOOT hold, a latched
  NVS request and software restart into enrollment; the latch survives an
  unexpected reset and is cleared only on an explicit portal exit;
- Ubuntu, hardware and regression documentation distinguishes application
  maintenance from ROM download mode;
- local Wi-Fi/cloud secret files remain ignored by Git;
- API v2 contract and ADR 0001 provide a stable boundary for Phase 2.

Automated release gate:

- ESP-IDF 6.0.2 firmware build: PASS;
- attendance persistence tests: PASS;
- HTTP payload serialization tests: PASS;
- Apps Script API/migration tests: PASS;
- `git diff --check`: PASS.

External gate still requiring the physical board owner:

1. Flash without `erase-flash` and verify identities/history survive.
2. Hold BOOT for 2.5 seconds while recognition is running, without pressing
   RST, and verify the restart into enrollment.
3. Return to recognition and verify the two-student flow once more.

The stable `v1.2.1-phase1` tag must point to a commit that has passed those
three checks. It is intentionally not created from an unverified dirty tree.

## P2.1 — Data model and API v2

Implemented in source:

- idempotent migration from the nine-column Phase 1 `Attendance` sheet;
- six-sheet schema: `Attendance`, `Students`, `Classes`, `Sessions`, `Devices`,
  `AuditLog`;
- request router for `attendance`, `heartbeat`, `session_sync`;
- stable error model with retryable/non-retryable classification;
- legacy Phase 1 attendance payload compatibility;
- conflict-safe Event ID idempotency;
- automatic session resolution by class, device and event timestamp;
- per-device SHA-256 secret hashes, enable/disable and secret rotation time;
- temporary `API_TOKEN` migration path controlled by `ALLOW_LEGACY_TOKEN`;
- firmware attendance payload upgraded to API v2 without changing the durable
  binary outbox format, so queued Phase 1 events remain readable;
- shared firmware version in boot logs and cloud payloads (currently
  `2.4.0-phase2-dev`);
- host test for JSON escaping and all required firmware payload fields.

P2.1 automated tests cover:

- migration and repeat migration;
- schema mismatch with no partial migration;
- legacy and v2 attendance;
- duplicate and Event ID conflict;
- wrong secret, rotated secret and disabled device;
- heartbeat update without duplicate device rows;
- historical session resolution, active session sync and session conflict;
- unsupported version/type and server-busy response.

Integration gate before P2.1 is marked complete:

1. Deploy the new Apps Script to a staging spreadsheet.
2. Run `setup`, `migrateToV2`, then `provisionDeviceFromProperties`.
3. Flash the Phase 2 development firmware.
4. Verify one real attendance row contains Session ID, firmware version and
   API version `2`.
5. Verify an old queued event is accepted exactly once.

See `P2_1_TEST_GUIDE.md` for the exact procedure.

## P2.2 — Sessions and teacher dashboard

Implemented in source:

- responsive Apps Script HTML Service dashboard;
- separate hashed dashboard access key despite the public device Web App;
- session creation, selection and close workflow;
- filters for class, date, session and student;
- Present/Late/Absent/Excused calculation from roster, threshold and events;
- UTF-8 CSV export;
- manual correction with actor/reason and immutable `Attendance` events;
- numeric-only Session IDs are stored as text and legacy date-coerced IDs are
  repaired by migration without changing their logical identifier;
- expired active sessions close on dashboard/API access and through one
  idempotently installed five-minute Apps Script trigger;
- correction and manual-close controls expose pending, success and actionable
  failure feedback instead of silently leaving stale controls;
- `SESSION_CREATE`, `SESSION_STATUS_CHANGE` and `ATTENDANCE_OVERRIDE` audit
  records;
- protection against more than one active session per device;
- safe DOM rendering without `innerHTML`, token in URL or persistent browser
  storage.

Automated P2.2 tests cover two roster students, historical/active sessions,
late/absent totals, correction persistence, immutable source events, numeric
ID date coercion, manual and automatic close, trigger idempotency, session
conflicts, session create/close audit and dashboard HTML syntax/security.

Local verification completed on 2026-09-22:

- Apps Script API v2/server tests: PASS;
- dashboard HTML syntax and security checks: PASS;
- attendance and HTTP payload host tests: PASS;
- ESP-IDF firmware build: PASS (`robot_attendance.bin` size `0x45db20`, 27%
  of the app partition free);
- desktop and mobile-size static dashboard render: PASS.

Integration gate before P2.2 is marked complete:

1. Add `Dashboard.html` to the staging Apps Script project.
2. Provision the dashboard access key and update the deployment.
3. Populate a real two-student roster.
4. Execute `P2_2_TEST_GUIDE.md` on laptop and phone.

## P2.3 — Runtime provisioning and configuration rollback

Implemented in source:

- firmware builds without `wifi_time_config.h` or `cloud_config.h`;
- runtime Wi-Fi, Apps Script URL, device ID and device secret live in an NVS
  A/B store with schema version, generation and CRC32;
- the active-slot pointer is committed only after the inactive candidate has
  been written, committed and read back successfully;
- maintenance portal runs in AP+STA mode, scans 2.4 GHz networks and keeps the
  local UI reachable while testing a candidate;
- candidate validation runs in a background job so the HTTP acceptance reply
  is sent before the shared Wi-Fi radio may change channel; the page polls the
  job after reconnecting and restart is an explicit final action;
- a candidate must obtain an IP, synchronize NTP and pass an authenticated
  Apps Script heartbeat before it can become active;
- invalid Wi-Fi/cloud credentials never replace the active configuration;
- explicit rollback selects the other valid slot;
- configuration-only factory reset clearly preserves identities, local
  attendance and the upload outbox;
- portal responses/status never return Wi-Fi passwords or device secrets;
- configured devices derive the maintenance AP password from the device
  secret, while an unconfigured device uses a documented bootstrap password.

Automated P2.3 gate:

- config validation, record version and CRC corruption tests: PASS;
- active-slot/power-loss selection tests: PASS;
- heartbeat JSON serializer tests: PASS;
- transient/truncated JPEG frames are discarded and retried three times;
  repeated pipeline failures reinitialize the camera, while TLS upload runs on
  core 1 away from the core-0 recognition loop;
- ESP-IDF 6.0.2 build: PASS (`robot_attendance.bin` size `0x463610`, 27%
  of the app partition free).

Integration gate before P2.2/P2.3 are marked complete:

1. Deploy the latest Apps Script and dashboard.
2. Flash P2.3 without `erase-flash`.
3. Execute `P2_2_P2_3_FULL_FLOW_TEST_GUIDE.md` on the real board, laptop and
   phone.
4. Preserve the final serial markers and Sheet/AuditLog evidence.

## P2.4 — Device health and operational resilience

Implemented in source:

- immediate heartbeat after network/time readiness, then every 60 seconds;
- uploader remains pinned to core 1 and processes at most five attendance
  events per batch so heartbeat cannot starve;
- telemetry includes uptime, RSSI, internal heap, PSRAM, pending events, last
  sync latency, accumulated failure count and last error;
- Apps Script materializes ONLINE/OFFLINE/DISABLED from server-side Last Seen,
  with a 180-second offline threshold and an idempotent one-minute trigger;
- teacher dashboard exposes device health without returning the secret hash;
- exponential backoff starts at five seconds, adds ±25% jitter and caps at
  300 seconds;
- outbox v2 adds CRC32 while preserving read/migration support for v1 records;
- repair scans past corrupt headers, discards truncated/checksum-invalid
  records and retains later valid events;
- recovery heartbeat reports pending failures before clearing diagnostics.

Automated P2.4 gate:

- attendance, payload, config, CRC/corruption and backoff host tests: PASS;
- Apps Script API, migration, online/offline and dashboard tests: PASS;
- ESP-IDF 6.0.2 build: PASS (`robot_attendance.bin` size `0x463e50`, 27%
  of the app partition free).

Hardware/deployment gate before P2.4 is marked complete:

1. Deploy latest `Code.gs` and `Dashboard.html`.
2. Run `migrateToV2` and `installPhase2Automation`.
3. Flash without `erase-flash`.
4. Execute `P2_4_TEST_GUIDE.md`, including ONLINE → OFFLINE → ONLINE and an
   offline event surviving reboot exactly once.

## P2.5 — OTA with rollback

Implemented in source:

- two 6 MiB application slots plus `otadata` on the 16 MiB module;
- the existing NVS and `faces` offsets remain unchanged across the P2.4 → P2.5
  migration, preserving runtime config, identities, local history and outbox;
- authenticated Apps Script `ota_manifest` endpoint with an explicit
  `OTA_ENABLED` release switch;
- HTTPS-only binary download using the ESP certificate bundle;
- embedded app version, Content-Length, received byte count and raw binary
  SHA-256 are all checked before the boot partition changes;
- recognition pauses and cloud uploads are serialized during the OTA flash
  write, avoiding concurrent camera/TLS pressure;
- rollback support is enabled in the bootloader; a new image becomes VALID
  only after NVS, SPIFFS, PSRAM, camera and face engine initialization;
- a dedicated compile-time rollback test image fails before confirmation;
- configurable build version, packaging script and CI binary/checksum/manifest
  artifacts.

Automated P2.5 gate:

- manifest parser boundary/hash/HTTPS tests: PASS;
- Apps Script disabled/available/current/invalid manifest tests: PASS;
- partition preservation and rollback source invariants: PASS;
- full Phase 2 host and Apps Script regression: PASS;
- ESP-IDF 6.0.2 build: PASS (`robot_attendance.bin` size `0x47ac80`, 25%
  of each OTA slot free).

Physical gate required to close Phase 2:

1. Flash P2.5 once without `erase-flash` and prove P2.4 data survives.
2. Reject a wrong checksum and recover after power loss during download.
3. OTA to a new version, then downgrade successfully.
4. Install the intentional rollback-test image and prove automatic return to
   the previously VALID slot.
5. Run two-student attendance after OTA and retain logs/dashboard evidence.

See `P2_5_TEST_GUIDE.md`. Until these hardware cases pass, P2.5 is source-
complete but Phase 2 is not yet release-closed.

## Chuyển tiếp sang Phase 3

Sau lượt test thực tế, người dùng xác nhận Phase 2 đủ ổn định để chuyển sang
Phase 3. DFPlayer Mini hiện được gán UART1 trên GPIO41/42, nguồn 5V và GND chung;
thiết kế, tiến độ và acceptance gate nằm trong PHASE3_PLAN.md,
PHASE3_PROGRESS.md và P3_TEST_GUIDE.md.
