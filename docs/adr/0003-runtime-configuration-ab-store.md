# ADR 0003: Runtime configuration uses a validated NVS A/B store

- Status: Accepted
- Date: 2026-09-22

## Context

Phase 1 embedded Wi-Fi and cloud credentials in ignored local headers. That
made every credential or endpoint change require a rebuild and could not
guarantee recovery from a power loss while saving new settings.

The ESP32 must keep existing identities, local attendance and its durable
outbox when network configuration changes or is reset.

## Decision

Runtime configuration contains Wi-Fi SSID/password, the Apps Script `/exec`
URL, device ID and device secret. It is stored as one versioned record with a
generation counter and CRC32 in one of two NVS slots.

An update follows this transaction:

1. validate field bounds and endpoint/device-ID rules;
2. connect using RAM-only ESP Wi-Fi driver storage;
3. obtain an IP address and synchronize NTP;
4. send an authenticated API v2 heartbeat to the candidate endpoint;
5. write and commit the inactive NVS slot;
6. read it back and verify schema/checksum;
7. switch and commit the active-slot pointer;
8. restart before normal operation uses the new configuration.

If power is lost before step 7, a still-valid requested active slot wins even
when the inactive slot has a higher generation. An explicit rollback selects
the other valid slot. Configuration reset erases only the configuration
namespace.

The portal is reachable only when configuration is missing or after a physical
BOOT-button maintenance request. The SoftAP uses WPA2. For teacher handoff,
both first boot and maintenance use a local build-time AP password from a
git-ignored header. Public development builds retain a documented fallback
and are not deployment images. HTTP status responses expose safe metadata
only and never return passwords or secrets.

## Consequences

- firmware and CI build without local secret headers;
- a bad Wi-Fi password, endpoint or device secret cannot replace the active
  configuration;
- endpoint changes do not require reflashing;
- NVS corruption is detected before a record is used;
- the previous working configuration remains available for one-step rollback;
- rotating a cloud secret requires coordinated Apps Script provisioning and a
  maintenance-portal update;
- the documented bootstrap password is suitable for first setup only; products
  deployed outside a controlled classroom should add a printed per-device
  pairing code or secure provisioning transport.
