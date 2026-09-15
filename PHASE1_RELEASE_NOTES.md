# Phase 1 release-candidate notes

Firmware version: `1.2.1-phase1-rc1`

This candidate closes the static-analysis gaps found after the initial Phase 1
implementation. It is not a final release until every hardware acceptance
item in `PHASE1_TEST_GUIDE.md` has evidence from the target board.

## Reliability fixes

- Persist cloud-bound events in the durable outbox before accepting attendance.
- Sync local CSV writes to flash and rotate history at 256 KiB.
- Preserve a previous CSV generation and restore cooldown state from both.
- Refuse attendance for a recognized face with missing student metadata.
- Detect a face/student database count mismatch and force maintenance mode.
- Refuse automatic NVS erase on recoverable initialization errors.
- Allow first-boot SPIFFS formatting only when the partition is completely
  erased; never auto-format a non-blank filesystem after a mount failure.
- Report SPIFFS usage and low-space state at boot.

## Enrollment and security fixes

- Reopen enrollment by holding the BOOT button at startup.
- Require a WPA2 enrollment password; open enrollment APs are no longer
  accepted.
- Stop printing the enrollment password in serial logs.
- Add an explicitly confirmed identity reset for database recovery.
- Require face/student counts to agree before completing enrollment.
- Require a configured Apps Script token and validate all cloud payload fields.

## Build and performance fixes

- Correct the target-board documentation: the camera is connected internally
  through its FPC socket, the plain status LED is GPIO2, and GPIO48 is a WS2812.
- Use the active ESP-IDF/Python interpreter instead of a Windows-only
  `python.exe` path when packing the face model.
- Fix JPEG decode input to the RGB565 little-endian format used by the official
  Espressif pipeline; remove four detector passes per idle frame.
- Add host-side attendance persistence tests, Apps Script contract tests, and
  an Ubuntu-based ESP-IDF 6.0.2 CI build with a binary artifact.

## Release gate

Do not rename this candidate to a final Phase 1 release or tag it as stable
until the manual camera, enrollment, recognition, power-loss, network, TLS and
Google Sheets tests all pass on the physical ESP32-S3 N16R8 board.
