# Architecture

## Hardware

```
Home Assistant  <--WiFi/API-->  ESP32 (ESPHome)  <--RS485-->  MAX485  <--RS485 bus-->  Actron System Controller
                                                                                          + Wall Remote 66 03
```

- ESP32 UART -> MAX485: `GPIO26` (TX) -> `DI`, `GPIO27` (RX) -> `RO`.
- `GPIO25` -> MAX485 `DE` and `RE` tied together: LOW = receive, HIGH =
  transmit. This is the only pin the firmware actively toggles; the UART
  hardware itself just streams bytes.
- MAX485 `A`/`B` -> the Actron RS485 bus (shared with the System Controller
  and the real Wall Remote 66 03).

Wire the AC's low-voltage RS485 bus only. Do not open or wire into the
Actron system's mains-voltage compartments -- that's outside this
document's scope and outside what a hobbyist interface project should
touch.

## Firmware structure

`ActronClassic` (`actron_classic.h`/`.cpp`) is the hub `Component` +
`UARTDevice`. It owns:

- **Byte-level framing** (`feed_byte_`, `push_header_window_`): an 8-byte
  rolling window is checked against three known 8-byte patterns (the PL1
  header, and the two poll frames we care about) on every idle byte. On a
  PL1 header match it switches into a fixed-length read of the remaining
  245 bytes; on a poll-for-`67 03` match it immediately calls `send_pl4_()`.
  No `delay()` calls anywhere -- everything is driven from bytes actually
  available in `loop()`, so the 300 ms poll-response budget is dominated by
  UART transmission time (239 bytes at 9600 baud 8N1 is ~249 ms), not by
  firmware latency.

- **PL1 decoding + debounce** (`decode_and_publish_pl1_`): checksum is
  verified first; a failed checksum discards the frame entirely (nothing is
  published, matching the source document). Telemetry-only fields
  (indoor/outdoor coil temp, compressor speed %, indoor fan RPM) go through
  a 5-consecutive-identical-readings debounce (`Debounce<T>` template)
  before publishing. Set-points and discrete/running states publish
  immediately.

- **PL4 construction** (`build_pl4_`): copies the static header, the
  documented duplicate-from-PL1 segments, and the static `00 FF FF` block,
  then overlays whatever actions are currently in flight, then computes the
  checksum. See `PROTOCOL_REFERENCE.md` for the exact byte table.

- **Action queue** (`action_queue_`, `in_flight_[2]`): HA-initiated actions
  (from the climate entity, the two zone switches, and the two fan selects)
  are queued, not applied immediately. Up to 2 actions are "in flight" at
  once, embedded into every PL4 response until the next PL1 confirms them
  or 3 retry cycles are exhausted (see `PROTOCOL_REFERENCE.md` assumption
  #3 for why combining 2 actions never collides with the reserved Power
  code). A same-type action arriving while one is still queued (not yet in
  flight) replaces it in place; the queue is capped at 10 pending entries,
  beyond which new actions are dropped and the entity reverts to whatever
  PL1 currently says.

- **Optimistic UI + reconciliation**: when HA requests a change, the
  relevant entity's `control()`/`write_state()` publishes the requested
  value immediately (so the UI feels responsive) while the action queue
  works in the background. `decode_and_publish_pl1_()` deliberately *skips*
  publishing an entity's value while that entity has an unconfirmed
  in-flight action, so the optimistic value isn't overwritten mid-flight by
  stale PL1 data. Once the action resolves (success or final failure), the
  entity is synced to the true PL1 value on the next decode pass.

## Entity layout

- `climate`: System Power (folded into `HVACMode.OFF`), System Mode,
  Setpoint, current temperature (from Temperature Actual), and a derived
  `action` (heating/cooling/fan/idle) for a richer climate card. Fan mode
  and fan speed are **not** part of the climate entity.
- `select` x2: Fan Mode (`Normal`/`Continuous`) and Fan Speed
  (`Low`/`Med`/`High`), independently selectable per the source document.
  Changing either recomputes the combined Actron fan byte and queues a
  single Fan action.
- `switch` x2: Zone 1, Zone 2.
- `sensor`: coil temperatures, compressor speed %, indoor fan RPM, plus
  diagnostics (checksum failures, poll responses sent, last response
  latency, action queue depth, action retries, seconds since last valid
  PL1).
- `binary_sensor`: reversing valve, compressor contactor, run request,
  system power, system running, zone 1/2 status (duplicates of the switch
  state, kept as separate diagnostic-category sensors so the raw bus
  reading is visible independent of the optimistic switch state), and bus
  data stale.
- `text_sensor`: system clock (`HH:MM:SS`), diagnostic-category so it
  doesn't clutter the main dashboard but is still available to automations
  (e.g. comparing it to `now()` to detect clock drift).

## What "unavailable after 10s stale" actually means here

ESPHome doesn't expose a simple per-entity "mark this unavailable while the
device stays connected to HA" API for sensor/binary_sensor/switch/select
platforms (that's different from the device itself going offline, which HA
already shows as unavailable automatically). Rather than fake it or ignore
the requirement, this implementation exposes a `AC Bus Data Stale` binary
sensor that turns on 10 seconds after the last valid PL1, and pauses all
entity updates while it's on (so no entity silently drifts from reality).
If you want HA to show the AC's entities as literally "unavailable" when
this happens, template that behaviour in Home Assistant using this binary
sensor -- see `HOME_ASSISTANT_SETUP.md` for an example.
