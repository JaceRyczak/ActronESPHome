# Protocol reference (as implemented)

Source: `AC Design Prompt.docx` (Actron Series 2 Classic RS485 control bus).
This document restates the protocol exactly as coded in
`esphome/components/actron_classic/protocol.h`, and flags every place the
implementation had to fill a gap the source document left open.

## Bus basics

- RS485, 9600 baud, 8 data bits, no parity, 1 stop bit.
- Half-duplex, single master (the System Controller). All Wall Remotes,
  including this interface, only ever speak when polled.
- Up to 3 Wall Remote addresses: `66 03`, `67 03`, `68 03`. Only `66 03` is a
  real, physically installed remote. This interface impersonates `67 03`.

## Bus cycle

1. System Controller broadcasts **PL1** (253 bytes, full state snapshot).
2. System Controller polls `66 03` (8 bytes) -> real Wall Remote answers with
   **PL2** (239 bytes). This interface does not need to parse PL2; it only
   needs PL1.
3. System Controller polls `67 03` (8 bytes) -> **this interface must answer
   within 300 ms** with **PL4** (239 bytes, its "button press" response).
4. System Controller polls `68 03` (8 bytes) -> nothing answers (not
   installed); the controller times out and starts the next cycle.

## PL1 (System Controller -> bus, 253 bytes)

Starts with the fixed header `00 10 00 04 00 7A F4 01`.

| Field | Bytes | Encoding |
|---|---|---|
| Header | 0-7 | fixed `00 10 00 04 00 7A F4 01` |
| System Status | 19 | `0x00` On, `0x02` Off |
| System Mode | 20 | `0x00` Off (indication only), `0x01` Cool, `0x02` Heat, `0x03` Fan, `0x04` Auto |
| (unknown) | 21 | not decoded |
| Fan Mode/Speed | 22 | `0x01/02/03` Low/Med/High, `0x81/82/83` same + Continuous |
| Setpoint | 23-24 | big-endian, x10 degC |
| Temperature Actual | 25-26 | big-endian int16, x10 degC |
| Frame delimiter | 31-32 | `75 30`, sanity check only |
| Indoor temp (duplicate) | 33-34 | unused |
| Indoor Coil Temperature | 35-36 | big-endian int16, x10 degC |
| Hardware status | 37 | high nibble `0x1_`=Reverse / `0x0_`=Normal; low nibble `0x_8`=Compressor On / `0x_0`=Off |
| System Clock | 41-43 | hour, minute, second |
| Call for Run / Run Request | 47-48 | `00 00` idle, `00 02` call for run |
| Compressor Speed | 49-50 | big-endian, value **is** the percent (`0x64` = 100) |
| Indoor Fan RPM | 51-52 | big-endian, decimal as-is |
| Frame delimiter | 53-54 | `75 30`, sanity check only |
| Outdoor Coil Temperature | 55-56 | big-endian int16, x10 degC |
| Zone status | 170 | bitmap, zone 1 = bit 0, zone 2 = bit 1 |
| Checksum | 251-252 *(confirmed, see below)* | CRC-16/Modbus, little-endian |

## PL4 (this interface -> bus, 239 bytes, sent only when polled as `67 03`)

Built fresh for every poll response from the most recently validated PL1:

| PL4 bytes | Source | Notes |
|---|---|---|
| 0-4 | static `67 03 EA 00 00` | identifies us as remote 67 03 answering |
| 5-10 | PL1 19-24 | system status/mode/fan/setpoint duplicated |
| 11-13 | static zero | |
| 14 | **action byte** | `0x00` = no action, else sum of active action codes (see below) |
| 15-18 | static zero | |
| 19-60 | PL1 34-75 | |
| 61-64 | static zero | |
| 65-149 | PL1 79-163 | |
| 150-152 | static `00 FF FF` | |
| 153-173 | PL1 167-187 | includes byte 156 (zone setting) and 170's duplicate position |
| 174-188 | static zero | |
| 189-236 | PL1 204-251 | |
| 237-238 | checksum | CRC-16/Modbus over bytes 0-236, little-endian |

Because every action's target byte position also falls inside one of the
duplicated segments above, the general duplication pass already gives PL4 a
correct "no change" baseline; the action-specific step in
`ActronClassic::build_action_byte_and_bytes_()` only has to overwrite the
handful of bytes an actual button press would change. This mirrors how a
real Wall Remote's response works and was a useful cross-check that the
table in the source document is internally consistent.

### Action byte (PL4 byte 14) and target bytes

| Action | Byte 14 bit | Target byte(s) | Value written |
|---|---|---|---|
| Mode | `0x01` | 6 | `SystemMode` (Cool/Heat/Fan/Auto -- never Off) |
| Fan | `0x02` | 8 | `FanModeSpeed` byte |
| Temperature | `0x04` | 9-10 | big-endian, x10 degC |
| Power | `0x07` | 5 AND 6 | byte 5: `0x00` On / `0x02` Off; byte 6: `MODE_OFF` on Off, target `SystemMode` on On |
| Zone | `0x40` | 156 | current zone byte (from PL1 170) with the target zone's bit set/cleared |

Multiple simultaneous actions add their bit values together (max 2 actions
per PL4, enforced by the action queue).

## Points not explicit in the source document

1. **PL1 checksum position -- confirmed.** The brief says PL1 "includes a
   CRC-16/Modbus checksum in little endian order" but never gives the byte
   offset. PL4's layout *is* fully specified (checksum at the last 2 bytes,
   237-238, covering bytes 0-236 of 239). This implementation was built on
   the assumption that PL1 follows the same pattern -- last 2 bytes
   (251-252), little-endian, covering bytes 0-250 of 253 -- and the
   requester has since confirmed this is correct. The checksum-failure
   diagnostic sensor (`AC Bus Checksum Failures`) remains in place as a
   field sanity check regardless (it should stay at or near zero in normal
   operation).

2. **"Run Request" = "Call for Run".** The entity list (Section 1) names a
   "Run Request" entity; the byte map (Section 2) only defines "Call for
   Run" (bytes 47-48). Confirmed by the requester: these are the same
   signal. Implemented as one binary sensor, `run_request`.

3. **Power (`0x07`) combining with other actions.** `0x07` numerically
   equals `0x01 + 0x02 + 0x04` (Mode+Fan+Temp), which looks like a collision
   in the additive action-byte scheme. Confirmed by the requester: this
   never actually happens because the document caps concurrent actions per
   PL4 at 2, and no 2-action sum of `{0x01, 0x02, 0x04, 0x40}` equals `0x07`
   (`0x01+0x02=0x03`, `0x01+0x04=0x05`, `0x02+0x04=0x06`). Power is
   therefore allowed to combine freely with any other single action.

4. **PL4 byte 5 (Power setting) encoding -- confirmed.** The document says
   the byte at the "updated setting" position must "correspond with data
   described later in this document" but doesn't spell out on/off values
   for the Power row specifically (unlike Mode/Fan/Temp, which each reuse an
   encoding Section 2 states explicitly elsewhere). Byte 5 sits inside the
   segment that normally duplicates PL1's System Status byte (19), so this
   implementation reused that byte's own encoding: `0x00` = On, `0x02` =
   Off. The requester has since confirmed this is correct.

   **CORRECTION (Claude Feedback, 2026-08-15):** the source design document
   was updated to clarify that Power actually requires writing *two* PL4
   bytes, not byte 5 alone: on On->Off, byte 5 = `0x02` and byte 6 =
   `MODE_OFF` (`0x00`); on Off->On, byte 5 = `0x00` and byte 6 = the
   `SystemMode` being entered (Cool/Heat/Fan/Auto). Earlier revisions only
   wrote byte 5, leaving byte 6 as an unchanged duplicate of whatever mode
   was last seen on PL1 -- this was the suspected root cause of the
   previously-reported bug where selecting "Off" did not actually turn the
   system off. Both bytes are now written together for the Power action;
   see `PL4_OFF_POWER_SETTING` / `PL4_OFF_MODE_SETTING` in `protocol.h` and
   the `POWER` case in `ActronClassic::build_action_byte_and_bytes_()`. Not
   yet verified against real hardware.

5. **"System State (Idle/Running)" derivation.** Defined as "Running if Fan
   speed is > 0 OR Call for Run is TRUE." Implemented using the *measured*
   Indoor Fan RPM (bytes 51-52), not the user's selected Fan Speed setting
   (byte 22) -- the former is an actual running-state signal, the latter is
   just a setting that persists whether or not the unit is on. Change
   `decode_and_publish_pl1_()` in `actron_classic.cpp` if this should mean
   something else.

6. **Stale-bus handling.** Not specified for anything past first-boot
   startup. Confirmed timeout: after 10 seconds with no valid PL1, a
   diagnostic binary sensor (`AC Bus Data Stale`) turns on and entity
   updates pause (holding last known values) rather than continuing to
   report data that may no longer be true. See `ARCHITECTURE.md` for why
   this isn't a literal Home Assistant "unavailable" state.

Everything else in the tables above is taken directly from the source
document without interpretation.
