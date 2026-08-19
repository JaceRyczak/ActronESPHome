# Testing plan

This was written and schema-validated (`esphome config`) in a sandboxed
environment without access to the ESP-IDF toolchain or real Actron
hardware, so nothing here has been run against a live bus yet. Work through
these in order -- each stage catches a different class of problem before
you risk it against a real, installed AC system.

## Stage 0 -- Compile

```
cd esphome
cp secrets.yaml.example secrets.yaml   # fill in real values
esphome compile actron-ac.yaml
```

Fix any C++ errors here first. The Python/schema layer was validated in
development (`esphome config` passes cleanly); the C++ layer was reviewed
by hand but not compiled against the real ESP-IDF headers.

## Stage 1 -- Bench test without the AC (simulated bus)

You don't need the Actron system connected to test framing, checksums, and
timing. Use a second microcontroller (or a USB-RS485 adapter + a small
Python script) to play back a scripted bus cycle to the ESP32's RS485 port:

1. Send a valid PL1 (253 bytes, correct header, correct checksum). Confirm
   in the ESPHome logs (`logger` is set to `DEBUG`) that it's accepted and
   entities move from `unknown` to real values.
2. Send a PL1 with a deliberately corrupted checksum. Confirm it's
   discarded (log line, `AC Bus Checksum Failures` increments, entities do
   **not** change).
3. Send the `67 03` poll frame. Confirm the ESP32 responds with 239 bytes
   within 300 ms -- an oscilloscope or logic analyzer on the DE/RE pin and
   TX line gives you an exact timestamp; alternatively, time it from the
   `AC Poll Response Latency` diagnostic sensor.
4. Send the `66 03` and `68 03` poll frames. Confirm the ESP32 does **not**
   respond to either.
5. Repeat step 1 with different byte values for each documented field
   (System Status, System Mode, Fan byte incl. both continuous and
   non-continuous, Setpoint at 16.0/23.5/30.0, negative temperatures for
   Temperature Actual / coil temps, Reversing Valve both states, Compressor
   Contactor both states, Call for Run both states, both zone bits in all 4
   combinations) and confirm each Home Assistant entity shows the expected
   decoded value. Cross-check every value against the byte table in
   `PROTOCOL_REFERENCE.md`.
6. Debounce check: send 4 consecutive PL1s with a *changed* indoor coil
   temperature, then a 5th matching the first 4. Confirm the sensor does
   **not** update until the 5th, but that Setpoint/Mode/etc (non-debounced)
   update immediately on the first changed PL1.
7. Stale-data check: stop sending PL1 entirely. Confirm `AC Bus Data Stale`
   turns on after 10 seconds and entities stop updating (holding last
   values) rather than resetting to unknown/zero.

## Stage 2 -- Action round-trip (still simulated bus)

For each action type, drive it from the Home Assistant UI (or a service
call) and confirm on the simulated-bus side that PL4's byte 14 and target
byte(s) match `PROTOCOL_REFERENCE.md`:

- Power on/off (via climate `HVACMode.OFF` <-> any other mode)
- Mode change while already on (confirm **only** a Mode action is sent, not
  Power+Mode -- this is the worked example from the design brief)
- Mode change while off (confirm Power+Mode are sent together in one PL4)
- Temperature up/down in 0.5 degC steps, and clamping at 16.0/30.0
- Fan Mode change (Normal <-> Continuous) at each fan speed
- Fan Speed change (Low/Med/High) at each fan mode
- Zone 1 on/off, Zone 2 on/off, and both together in the same PL4
- Two different action types queued in the same HA "moment" (e.g. change
  setpoint and mode within the same second) -- confirm both land in one
  PL4 (byte 14 = sum) if they arrive close enough together, or sequentially
  across two PL4s otherwise

## Stage 3 -- Verification, retry, and reversion

Using the simulated bus, deliberately have the "AC" **not** reflect a
requested change in subsequent PL1s:

1. Confirm the interface retries (re-embeds the same action in PL4) after 3
   PL1 cycles without confirmation.
2. Confirm it gives up after 3 total attempts and reverts the HA entity to
   whatever the simulated PL1 actually says.
3. Queue an 11th action while 10 are already pending (drive this via rapid
   HA service calls, or directly if you have a way to stall verification).
   Confirm the 11th is discarded and its entity reverts rather than getting
   queued.
4. Confirm a second change to the same entity while the first is still
   queued (not yet in flight) supersedes it rather than queuing twice.

## Stage 4 -- Real hardware, low-risk order

Only after Stages 0-3 pass:

1. Connect the ESP32/MAX485 to the real bus with the AC powered off at the
   isolator (RS485 data lines only -- see the wiring note in
   `ARCHITECTURE.md`). Confirm you see nothing (no PL1) since the System
   Controller isn't running.
2. Power the AC system on. Confirm PL1 is received, checksum passes, and
   every entity populates with plausible real-world values (compare a
   couple against the physical Wall Remote 66 03's display).
3. Confirm the ESP32 answers polls within budget on the real bus and that
   the real Wall Remote 66 03 continues working completely normally
   throughout (it should be entirely unaffected -- if it starts behaving
   oddly, disconnect the interface immediately and re-check Stage 1-3).
4. Try one read-only cycle (just watch entities track the physical remote's
   changes) before trying any interface-initiated action.
5. Try a single, low-consequence action first (e.g. toggle Zone 2 if
   nothing depends on it) and confirm on the physical Wall Remote / AC
   behavior that it actually took effect, before trying Power/Mode changes.

## Ongoing monitoring

Keep an eye on the diagnostic sensors after deployment:

- `AC Bus Checksum Failures` should stay near zero in normal operation --
  if it climbs steadily, revisit the PL1 checksum offset assumption in
  `PROTOCOL_REFERENCE.md`.
- `AC Poll Response Latency` should stay comfortably under 300 ms -- if
  it's close to the limit, investigate WiFi/loop() contention.
- `AC Action Retries` should be rare -- frequent retries suggest either the
  action encoding is wrong for that action type, or bus contention.
