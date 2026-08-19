# Actron Series 2 Classic <-> Home Assistant interface

An ESP32 + ESPHome interface that lets Home Assistant read and control an
Actron Series 2 Classic air conditioning system by impersonating a second
Wall Remote (address `67 03`) on its RS485 control bus.

Built from the design brief in `AC Design Prompt.docx`, plus a round of
clarification on points the brief left ambiguous (see "Assumptions" below).

## Contents

- `esphome/actron-ac.yaml` -- the device configuration. Copy into your
  ESPHome dashboard config folder (or point the dashboard at this repo).
- `esphome/secrets.yaml.example` -- copy to `secrets.yaml` and fill in.
- `esphome/components/actron_classic/` -- the custom C++ external component
  that implements the RS485 protocol, action queue, and Home Assistant
  entities.
- `docs/PROTOCOL_REFERENCE.md` -- the Actron bus protocol as implemented,
  including every place this project had to make an inference the source
  brief didn't spell out.
- `docs/ARCHITECTURE.md` -- how the firmware is structured and why.
- `docs/HOME_ASSISTANT_SETUP.md` -- steps to take in Home Assistant after
  flashing (nothing exotic, but a couple of things ESPHome doesn't automate).
- `docs/TESTING.md` -- a bench and field test plan, since this can't be
  compiled or run against real hardware from here.

## Quick start

1. Wire the ESP32 to a MAX485 module: `GPIO26` -> `DI`, `GPIO27` -> `RO`,
   `GPIO25` -> `DE` and `RE` (tied together). Wire the MAX485 `A`/`B` to the
   Actron system's RS485 bus.
2. `cp esphome/secrets.yaml.example esphome/secrets.yaml` and fill in your
   WiFi credentials and generate an API encryption key (command is in the
   file).
3. From the ESPHome dashboard (or CLI: `esphome run actron-ac.yaml`), build
   and flash the ESP32.
4. Follow `docs/HOME_ASSISTANT_SETUP.md` for the remaining HA-side steps.
5. Work through `docs/TESTING.md` before relying on it to control your AC.

## Validation performed in this environment

This sandbox has no internet access to the ESP-IDF/PlatformIO toolchain, so
a full `esphome compile` could not be completed here. What *was* verified:

- `esphome config actron-ac.yaml` -- passes cleanly (validates the Python
  codegen, entity schemas, and pin/UART configuration).
- The CRC-16/Modbus implementation was checked against the standard Modbus
  test vector (`01 03 00 00 00 0A` -> bytes `C5 CD` on the wire).
- Every byte offset and table in `docs/PROTOCOL_REFERENCE.md` was checked
  by hand against the source design document.

Run `esphome compile actron-ac.yaml` yourself before flashing -- treat this
as reviewed, not yet hardware-verified, code.

## Points worth knowing about

A few points in the source brief were structurally underspecified. Rather
than guess silently, each was worked out by inference, then confirmed with
the requester. Called out at its point of use in code comments and
summarized in `docs/PROTOCOL_REFERENCE.md`. In short:

- PL1's checksum is its last 2 bytes (offsets 251-252), little-endian --
  originally inferred by direct analogy to PL4's explicitly documented
  layout, now confirmed.
- "Run Request" is treated as the same signal as "Call for Run" (confirmed).
- Power (byte 14 = `0x07`) can safely combine with any other single action
  because the 2-action-per-PL4 cap makes the `0x07` collision unreachable
  (confirmed).
- PL4 byte 5 (the Power action's "updated setting" byte) uses the same
  `0x00`=On / `0x02`=Off encoding as PL1's System Status byte -- originally
  inferred from its position inside a duplicated PL1 segment, now confirmed.
- Bus data is considered stale, and a diagnostic binary sensor flips on,
  after 10 seconds without a valid PL1 (confirmed timeout). See
  `docs/ARCHITECTURE.md` for what "unavailable" actually means in ESPHome's
  entity model -- it's not a literal HA "unavailable" grey-out.
- "System State (Idle/Running)" is derived from measured Indoor Fan RPM
  (not the user's selected fan speed) OR Call for Run.
