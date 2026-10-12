# Edge Attendance System

An offline-tolerant classroom attendance prototype built with an ESP32-S3
camera board, on-device face recognition, DFPlayer audio, Google Apps Script,
and Google Sheets. The robot recognizes enrolled students without streaming
camera images to the cloud; a teacher manages morning/afternoon sessions from
a browser dashboard.

**Project status (12 Oct 2026):** firmware `3.1.0-phase3-ux-dev`, host tests,
Apps Script tests, and the ESP-IDF build pass. The owner has exercised the
two-student attendance, named audio, session close, device health, and long-run
flows on hardware. OTA/automatic rollback and the new emailed-CSV flow still
need recorded end-to-end acceptance; this is a prototype, not a certified
school deployment.

## How it works

```text
ESP32-S3 camera → face match → durable local outbox ── HTTPS ──► Apps Script
                             │                                  │
                             ├─ local attendance history         ├─ Google Sheets
                             └─ DFPlayer named greeting          ├─ teacher dashboard
                                                                └─ CSV email report
```

1. An administrator enrolls a face, name, and class through the robot's local
   `RobotSetup` portal. The camera is connected internally on this board; no
   jumper wires are needed for the camera.
2. The robot recognizes a face locally, applies a 60-second cooldown, and
   stages an event in flash *before* attempting HTTPS upload. Audio runs in a
   separate UART queue, so a missing speaker or SD card does not block
   attendance.
3. Apps Script authenticates the device and uses `event_id` for idempotent
   retries. Google Sheets holds append-only events, roster, sessions, device
   health, and an audit log. A teacher can review a session, record a justified
   correction, and export CSV without rewriting the original event.
4. A session closes manually or at its scheduled end. The backend can queue a
   CSV email to the address entered when the session was created.

## Engineering highlights

| Area | Implementation |
|---|---|
| Edge inference | ESP-DL face recognition on ESP32-S3-WROOM N16R8 with PSRAM; face images/embeddings remain on device, while attendance payloads contain name/class metadata. |
| Delivery guarantees | Versioned, CRC-checked flash outbox; retry/backoff and server-side `event_id` deduplication. The local history is diagnostic, not the upload queue. |
| Configuration | Wi-Fi and cloud credentials are entered at runtime, validated through Wi-Fi → NTP → authenticated heartbeat, then stored in a two-slot NVS A/B record with generation and CRC. |
| Teacher workflow | Authenticated dashboard for morning (07:00–10:30) and afternoon (14:00–16:30) sessions, roster counts, late/absent/excused states, audited corrections, and CSV export. |
| Operations | Device heartbeat, ONLINE/OFFLINE status, RSSI/heap/PSRAM/pending telemetry, and two-slot OTA with SHA-256 verification and a boot health gate. |
| Audio | DFPlayer Mini on UART1; `/mp3/1001.mp3` is the full greeting for Student ID 1, through ID 50. Volume defaults to the tested 30/30. |

## Hardware and stack

- ESP32-S3-WROOM N16R8 board with built-in camera connector and two USB-C
  ports. The tested pin map is Freenove/ESP32S3_EYE-compatible; verify the PCB
  before substituting a different board.
- DFPlayer Mini, microSD card, small speaker, common ground, and a 1 kΩ series
  resistor from ESP32 GPIO41 (TX) to DFPlayer RX. ESP32 GPIO42 (RX) receives
  DFPlayer TX. The speaker connects across SPK1/SPK2, not to ground.
- ESP-IDF 6.0.2, C/C++, ESP-DL, SPIFFS/NVS, HTTPS; Google Apps Script and
  Sheets for the API and teacher interface; Node.js/GCC for host tests.

See [hardware and Ubuntu setup](HARDWARE_AND_UBUNTU_GUIDE.md) before wiring or
flashing. Do not use `erase-flash` for a routine update: it can remove local
configuration, face data, and queued events.

## Build and test on Ubuntu

From the repository root, with ESP-IDF 6.0.2, Node.js, GCC, and Python 3:

```bash
bash tests/host/run_tests.sh
node tests/apps_script/test_code.js
node tests/apps_script/test_dashboard_html.js
cd firmware
eim run "idf.py build" v6.0.2
```

The firmware output is `firmware/build/robot_attendance.bin`. For a USB-UART
flash, first identify the actual port with `ls -l /dev/serial/by-id/`, then
run `eim run "idf.py -p PORT flash monitor" v6.0.2` from `firmware/`, replacing
`PORT` with that device path. The two portal pages live in
`firmware/main/portal_enroll.html` and `portal_config.html` and are embedded
into the image during build.

The automated suite covers persistence, outbox corruption/recovery, payloads,
OTA manifests, DFPlayer protocol, portal JavaScript, Apps Script API behavior,
and dashboard logic. It does **not** replace the
[hardware acceptance checklist](FULL_SYSTEM_ACCEPTANCE_TEST.md).

## Set up the backend and use the system

1. Follow the [Apps Script deployment guide](google_apps_script/README.md) to
   create the six-sheet schema, provision a device secret and teacher account,
   install the session/health/email triggers, and deploy the Web App. Update
   the existing deployment whenever `Code.gs` or `Dashboard.html` changes.
2. Connect to `RobotSetup`, open `http://192.168.4.1/config`, and enter the
   Wi-Fi details and Apps Script `/exec` URL. The device secret is an admin-only
   setup value; it is never returned to the browser. A new configuration is
   saved only after the connectivity checks pass. Enroll students at
   `http://192.168.4.1/`.
3. Prepare the named MP3 greetings using the robot's actual Student IDs, not
   row numbers in a class list. See the [audio roster guide](AUDIO_CLASS_ROSTER_GUIDE.md)
   and [DFPlayer test guide](P3_TEST_GUIDE.md).
4. A teacher signs in to the dashboard, starts the appropriate morning or
   afternoon session, enters the CSV recipient email, and reviews attendance.
   Corrections require a reason. The session closes at its end time or by
   teacher action; CSV export remains available. Email delivery is queued
   after close and should be checked in `Sessions.Report Status`.

## Repository map

```text
firmware/main/          device application, portal assets, upload, OTA, audio
firmware/components/    face-recognition component and model
google_apps_script/     API, Sheet schema, dashboard, deployment guide
tests/host/             firmware host and source tests
tests/apps_script/      Apps Script/dashboard tests
tools/                  SD preparation, audio planning, OTA packaging
docs/adr/               architecture decisions
```

The [API v2 contract](API_V2_CONTRACT.md) documents payloads, errors, and Sheet
columns. The [end-to-end checklist](FULL_SYSTEM_ACCEPTANCE_TEST.md) is the
source of truth for physical acceptance; test evidence with student data stays
outside Git.

## Security, privacy, and current limits

- Never commit face images/embeddings, real class rosters, exported CSV, serial
  logs containing private data, Wi-Fi passwords, device secrets, or production
  Apps Script properties. Local secret headers and build artifacts are ignored
  by Git. A public development image may contain a shared setup-AP password;
  use per-device credentials before any real school rollout.
- The Apps Script Web App must be reachable by the robot, but device requests
  are authenticated and dashboard data/actions require a teacher session.
  Treat the deployment URL and Sheet permissions as operational secrets.
- Named speech is **pre-recorded** per Student ID. The robot does not generate
  speech from a typed name. A roster change requires checking the SD mapping.
- Google Sheets/Apps Script are suitable for a small pilot, not a claim of
  high-scale or regulated production readiness. OTA/rollback and emailed CSV
  must pass real-device tests before a release is marked complete.

Next gates: complete OTA/rollback and email E2E evidence, verify a full class
roster and audio mapping, then harden setup credentials and deployment
operations for handover.
