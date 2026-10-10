# Phase 1 release notes

Firmware version: `1.2.1-phase1`

The end-to-end attendance flow has passed on the target board with two enrolled
students: recognition, cooldown, durable local event creation, HTTPS delivery
and Google Sheets idempotency. Destructive power-loss and long-duration soak
tests remain explicit regression items rather than implied results.

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

- Reopen enrollment by holding BOOT for 2.5 seconds while the application is
  running. A one-shot NVS flag and software restart avoid GPIO0's ROM download
  strap at reset.
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

## Baseline gate

Before tagging this baseline, rerun the automated suites and the marked core
hardware checks in `PHASE1_TEST_GUIDE.md`. Keep power-loss, network interruption
and long-duration tests visible in the release evidence even when they are not
part of the two-student happy-path result.
