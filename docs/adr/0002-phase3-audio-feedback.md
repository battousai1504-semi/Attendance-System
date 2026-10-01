# ADR 0002: Isolate DFPlayer audio behind a non-blocking queue

- Status: Accepted
- Date: 2026-09-26

## Context

Face capture/recognition is latency-sensitive and already shares memory, flash
and network resources with persistence, uploader and OTA. DFPlayer Mini is a
low-cost UART peripheral whose presence, SD card and clone-specific ACK
behavior cannot be assumed reliable.

Calling audio commands inline from recognition would let UART or peripheral
faults delay attendance. Treating audio as an OTA health dependency would also
roll back valid firmware because of an optional accessory.

## Decision

Use UART1 on GPIO41/42 and give one low-priority task exclusive ownership of
the UART. Producers enqueue semantic events without waiting. The audio task
maps events to fixed files under /mp3, applies volume and parses feedback.

Store audio settings separately from network/cloud config. Audio initialization
failure is logged and degraded, never fatal. Do not include DFPlayer in the OTA
health gate.

## Consequences

- Attendance remains correct and available without DFPlayer.
- Burst events can be deliberately dropped instead of blocking camera.
- Fixed filenames make the SD image and demo reproducible.
- Portal status is diagnostic; audible output remains necessary because some
  compatible modules may not return ACK consistently.
- GPIO41/42 are now reserved and future peripherals must use another resource.
