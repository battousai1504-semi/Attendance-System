# ADR 0001: Keep Apps Script as the Phase 2 application backend

- Status: Accepted
- Date: 2026-09-16

## Context

Phase 1 already has a working HTTPS path from ESP32-S3 to Google Apps Script
and an operator-visible Google Sheet. Phase 2 needs sessions, a teacher
dashboard, device health, runtime provisioning and eventually OTA. Replacing
the backend at the same time would increase deployment and authentication work
without improving face recognition or offline durability.

## Decision

Phase 2 keeps Google Apps Script and Google Sheets, adds a versioned API v2,
and serves the teacher dashboard with Apps Script HTML Service. The ESP32 owns
face images and embeddings; cloud storage receives attendance metadata only.

`Attendance` and `AuditLog` are append-only. Derived attendance status may be
recomputed from roster, session and events. Manual corrections create audit
records instead of changing the original event.

One device may serve multiple configured classes but may have only one active
session at a time.

## Consequences

- Existing deployment knowledge and the Phase 1 path remain reusable.
- A staging spreadsheet can exercise migrations without risking real data.
- Apps Script quotas and sheet scan cost set an upper bound; queries must avoid
  full-sheet scans on every heartbeat.
- A future server migration is possible because firmware talks to a documented
  JSON contract rather than sheet columns directly.
- Raw biometric data is not synchronized to the cloud.

## Deferred decisions

- OTA manifest hosting and signing policy are decided in P2.5.
- DFPlayer Mini, speaker power design and UART GPIO allocation belong to Phase
  3 and must be checked against the camera pin map before wiring.
