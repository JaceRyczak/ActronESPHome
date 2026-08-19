// protocol.h
//
// Actron Series 2 Classic RS485 control-bus protocol constants.
//
// The following points were not explicit in the source design document.
// Each was worked out by inference first, then confirmed directly by the
// requester -- noted here for anyone reading the code without the chat
// history:
//   1. PL1 (253 bytes) checksum occupies the LAST 2 bytes of the payload
//      (offsets 251-252), little-endian, covering bytes 0-250. (Inferred by
//      analogy to PL4's explicitly documented layout; confirmed correct.)
//   2. "Run Request" (Section 1 entity list) is the same signal as
//      "Call for Run" (Section 2, PL1 bytes 47-48). (Confirmed.)
//   3. PL4 byte 5 (the Power action's "updated setting" byte) uses the same
//      encoding as PL1's System Status byte: 0x00 = On, 0x02 = Off.
//      (Inferred from byte 5's position inside the PL1 19-24 duplicate
//      segment; confirmed correct.)
//
// CORRECTION (Claude Feedback, 2026-08-15): the source design document was
// updated to clarify that the Hardware Status field is a *byte pair*, bytes
// 37-38, not byte 37 alone -- byte 37 is expected to always read 0x00, and
// byte 38 is the one actually carrying the high-nibble (Reversing Valve) /
// low-nibble (Compressor Contactor) encoding. Earlier revisions of this file
// read byte 37 directly, which produced a Compressor Contactor / Reversing
// Valve reading that never changed on real hardware. PL1_OFF_HW_STATUS now
// points at byte 38.
//
// All other offsets below are taken directly from the design document.

#pragma once
#include <cstdint>
#include <cstddef>

namespace esphome {
namespace actron_classic {

// ---------------------------------------------------------------------------
// Frame sizes
// ---------------------------------------------------------------------------
static const size_t PL1_LEN = 253;  // System Controller -> bus, full state update
static const size_t PL4_LEN = 239;  // Our simulated Wall Remote 67 03 -> bus, response to poll
static const size_t POLL_FRAME_LEN = 8;

// PL1 always starts with this 8-byte magic
static const uint8_t PL1_HEADER[8] = {0x00, 0x10, 0x00, 0x04, 0x00, 0x7A, 0xF4, 0x01};

// Poll frames issued by the System Controller to each Wall Remote address.
// We only ever answer the 67 03 poll (we simulate Wall Remote #2, which per
// the brief is not physically installed).
static const uint8_t POLL_REMOTE_66[POLL_FRAME_LEN] = {0x66, 0x03, 0x00, 0x09, 0x00, 0x75, 0x5C, 0x38};
static const uint8_t POLL_REMOTE_67[POLL_FRAME_LEN] = {0x67, 0x03, 0x00, 0x09, 0x00, 0x75, 0x5D, 0xE9};
static const uint8_t POLL_REMOTE_68[POLL_FRAME_LEN] = {0x68, 0x03, 0x00, 0x09, 0x00, 0x75, 0x5D, 0x16};

// Max time from seeing POLL_REMOTE_67 to having PL4 fully written to the bus.
static const uint32_t POLL_RESPONSE_BUDGET_MS = 300;

// If no *valid* (checksum-passing) PL1 has been seen for this long, all
// entities are marked unavailable (user-confirmed behaviour).
static const uint32_t STALE_PL1_TIMEOUT_MS = 10000;

// Telemetry values (not set-points, not running/discrete states) must see
// this many consecutive identical raw readings before the decoded value is
// considered stable and published to Home Assistant.
static const uint8_t TELEMETRY_DEBOUNCE_COUNT = 5;

// Minimum time between "AC System Clock" publishes (Claude Feedback,
// 2026-08-15 change request: "Update the following entities to only update
// once an hour: System Clock").
static const uint32_t SYSTEM_CLOCK_PUBLISH_INTERVAL_MS = 3600000;  // 1 hour

// ---------------------------------------------------------------------------
// PL1 byte offsets (Section 2 of the design document)
// ---------------------------------------------------------------------------
static const size_t PL1_OFF_SYSTEM_STATUS = 19;       // 0x00 On, 0x02 Off
static const size_t PL1_OFF_SYSTEM_MODE = 20;          // 0x00 Off,0x01 Cool,0x02 Heat,0x03 Fan,0x04 Auto
static const size_t PL1_OFF_FAN_MODE_SPEED = 22;       // see FanModeSpeed enum below
static const size_t PL1_OFF_SETPOINT = 23;             // 2 bytes, big-endian, x10 degC
static const size_t PL1_OFF_TEMP_ACTUAL = 25;          // 2 bytes, int16 BE, x10 degC
static const size_t PL1_OFF_FRAME_DELIM_1 = 31;        // 75 30, sanity-check only
static const size_t PL1_OFF_INDOOR_TEMP_DUP = 33;      // duplicate of temp actual, unused
static const size_t PL1_OFF_INDOOR_COIL_TEMP = 35;     // 2 bytes, int16 BE, x10 degC
static const size_t PL1_OFF_HW_STATUS_RESERVED = 37;    // corrected 2026-08-15: expected always 0x00, part of the byte 37-38 pair
static const size_t PL1_OFF_HW_STATUS = 38;              // corrected 2026-08-15 (was 37): high nibble Reversing Valve, low nibble Compressor Contactor
static const size_t PL1_OFF_CLOCK = 41;                 // hour, minute, second (3 bytes)
static const size_t PL1_OFF_CALL_FOR_RUN = 47;          // 2 bytes: 00 00 idle, 00 02 call for run ("Run Request")
static const size_t PL1_OFF_COMPRESSOR_SPEED = 49;      // 2 bytes: 00 64 = 100%, 00 00 = 0%
static const size_t PL1_OFF_INDOOR_FAN_RPM = 51;        // 2 bytes, decimal as-is
static const size_t PL1_OFF_FRAME_DELIM_2 = 53;         // 75 30, sanity-check only
static const size_t PL1_OFF_OUTDOOR_COIL_TEMP = 55;      // 2 bytes, int16 BE, x10 degC
static const size_t PL1_OFF_ZONE_STATUS = 170;           // bitmap, zone1=LSB .. zone8=MSB

// PL1 segment used to build PL4 "action response" byte-14 verification: for
// each action type, which PL1 offset(s) reflect whether the action was applied.
static const size_t PL1_OFF_ZONE_STATUS_LEN = 1;

// System status values
static const uint8_t SYSTEM_STATUS_ON = 0x00;
static const uint8_t SYSTEM_STATUS_OFF = 0x02;

// System mode values
enum SystemMode : uint8_t {
  MODE_OFF = 0x00,
  MODE_COOL = 0x01,
  MODE_HEAT = 0x02,
  MODE_FAN = 0x03,
  MODE_AUTO = 0x04,
};

// Fan mode/speed byte (single byte encodes both dimensions)
enum FanModeSpeed : uint8_t {
  FAN_LOW = 0x01,
  FAN_MED = 0x02,
  FAN_HIGH = 0x03,
  FAN_CONT_LOW = 0x81,
  FAN_CONT_MED = 0x82,
  FAN_CONT_HIGH = 0x83,
};

static const uint8_t FAN_CONTINUOUS_BIT = 0x80;
static const uint8_t FAN_SPEED_MASK = 0x7F;  // 0x01/0x02/0x03 once continuous bit stripped

static const float SETPOINT_MIN_C = 16.0f;
static const float SETPOINT_MAX_C = 30.0f;
static const float SETPOINT_STEP_C = 0.5f;

// ---------------------------------------------------------------------------
// PL4 layout (Section 1, "Interface Approach" table)
// ---------------------------------------------------------------------------
// PL4 byte range -> source: static value, or a duplicated PL1 segment.
struct Pl4DupSegment {
  size_t pl4_offset;
  size_t pl1_offset;
  size_t length;
};

// Static header identifying us as Wall Remote 67 03 responding to a poll.
static const uint8_t PL4_STATIC_HEADER[5] = {0x67, 0x03, 0xEA, 0x00, 0x00};
static const size_t PL4_OFF_STATIC_HEADER = 0;

static const size_t PL4_OFF_ACTION_BYTE = 14;  // "Controller Action Transmit"

static const uint8_t PL4_STATIC_00FF_FF[3] = {0x00, 0xFF, 0xFF};
static const size_t PL4_OFF_00FF_FF = 150;

static const size_t PL4_OFF_CHECKSUM = 237;  // 2 bytes, little-endian, over bytes 0-236

// Duplicate-from-PL1 segments (everything else in PL4 that isn't static or
// the action byte is copied verbatim from the most recently validated PL1).
// Taken directly from the design document's PL4 construction table.
static const size_t PL4_DUP_SEGMENT_COUNT = 5;
static const Pl4DupSegment PL4_DUP_SEGMENTS[PL4_DUP_SEGMENT_COUNT] = {
    {5, 19, 6},      // system status/mode/fan/setpoint/temp-actual byte region
    {19, 34, 42},    // indoor temp dup .. through outdoor coil temp region
    {65, 79, 85},    // mid payload
    {153, 167, 21},  // zone status region (byte 170 lands inside this block)
    {189, 204, 48},  // tail region
};

// PL4 byte-14 "Controller Action Transmit" bit values (Section 1 table).
// NOTE: 0x07 (Power) numerically equals 0x01+0x02+0x04 (Mode+Fan+Temp) added
// together, which looks like a collision at first glance. User-confirmed
// reasoning: the design document caps concurrent actions per PL4 at 2, and
// no 2-action combination of {MODE=1, FAN=2, TEMP=4, ZONE=0x40} sums to 0x07
// (0x01+0x02=0x03, 0x01+0x04=0x05, 0x02+0x04=0x06), so the collision can
// never actually be constructed. Power may therefore be freely combined with
// any single other action type (e.g. Power+Mode = 0x08) -- see
// ActronClassic::build_action_byte() in actron_classic.cpp, which sums at
// most MAX_IN_FLIGHT_ACTIONS (2) active action byte values.
enum ActionByteBit : uint8_t {
  ACTION_BIT_MODE = 0x01,
  ACTION_BIT_FAN = 0x02,
  ACTION_BIT_TEMP = 0x04,
  ACTION_BIT_POWER = 0x07,
  ACTION_BIT_ZONE = 0x40,
};

// Updated-setting byte positions in PL4 for each simulated button press.
static const size_t PL4_OFF_ZONE_SETTING = 156;
static const size_t PL4_OFF_MODE_SETTING = 6;
static const size_t PL4_OFF_TEMP_SETTING_HI = 9;
static const size_t PL4_OFF_TEMP_SETTING_LO = 10;
static const size_t PL4_OFF_POWER_SETTING = 5;  // 0x00 = On, 0x02 = Off (confirmed)
static const size_t PL4_OFF_FAN_SETTING = 8;

// CORRECTION (Claude Feedback, 2026-08-15): the source design document was
// updated -- Power (byte 14 = ACTION_BIT_POWER) must update BOTH
// PL4_OFF_POWER_SETTING (byte 5) AND PL4_OFF_MODE_SETTING (byte 6), not
// byte 5 alone:
//   On  -> Off: byte 5 = SYSTEM_STATUS_OFF (0x02), byte 6 = MODE_OFF (0x00)
//   Off -> On:  byte 5 = SYSTEM_STATUS_ON  (0x00), byte 6 = the mode being
//               entered into (Cool/Heat/Fan/Auto)
// Earlier revisions only wrote byte 5, leaving byte 6 as an unchanged
// duplicate of the pre-action PL1 mode -- suspected root cause of "Off
// doesn't turn off the system" (the action queued and transmitted per the
// old byte layout, but the controller had no byte-6 signal that the mode
// setting had actually been actioned alongside the power setting). See
// ActronClassic::build_action_byte_and_bytes_() in actron_classic.cpp.

// Zone bitmap (Table 2): zone 1 = LSB, zone 2 = bit 1. Only 2 zones installed.
static const uint8_t ZONE1_BIT = 0x01;
static const uint8_t ZONE2_BIT = 0x02;

// ---------------------------------------------------------------------------
// Payload validation -- expected value sets for enumerated (non-continuous)
// PL1 fields. Claude Feedback (2026-08-15): added after the byte 37/38
// hardware-status field turned out to be misread against the design
// document (see the correction note near PL1_OFF_HW_STATUS above).
// ActronClassic::validate_pl1_fields_() checks every field below on each
// valid PL1 and reports a mismatch via the "AC Payload Validation Failures"
// counter and "AC Payload Validation Detail" text sensor, so a similarly
// wrong assumption elsewhere shows up as a diagnosable signal instead of a
// silently-wrong reading. Continuous fields (temperatures, compressor %,
// fan RPM, setpoint) aren't included here -- they don't have a fixed set of
// legal values to check against.
// ---------------------------------------------------------------------------
static const uint8_t VALID_SYSTEM_STATUS[] = {SYSTEM_STATUS_ON, SYSTEM_STATUS_OFF};
static const size_t VALID_SYSTEM_STATUS_COUNT = 2;

static const uint8_t VALID_SYSTEM_MODE[] = {MODE_OFF, MODE_COOL, MODE_HEAT, MODE_FAN, MODE_AUTO};
static const size_t VALID_SYSTEM_MODE_COUNT = 5;

static const uint8_t VALID_FAN_MODE_SPEED[] = {FAN_LOW, FAN_MED, FAN_HIGH, FAN_CONT_LOW, FAN_CONT_MED, FAN_CONT_HIGH};
static const size_t VALID_FAN_MODE_SPEED_COUNT = 6;

// Byte 38 only -- byte 37 (PL1_OFF_HW_STATUS_RESERVED) is checked separately
// against VALID_HW_STATUS_RESERVED. High nibble: 0x0_=Normal, 0x1_=Reverse.
// Low nibble: 0x_0=Off, 0x_8=On. All 4 combinations are legitimate states,
// including 0x00 (Normal + Off, i.e. idle).
static const uint8_t VALID_HW_STATUS[] = {0x00, 0x08, 0x10, 0x18};
static const size_t VALID_HW_STATUS_COUNT = 4;

static const uint8_t VALID_HW_STATUS_RESERVED[] = {0x00};
static const size_t VALID_HW_STATUS_RESERVED_COUNT = 1;

// 2-byte fields, big-endian, checked as one combined 16-bit value.
static const uint16_t VALID_CALL_FOR_RUN[] = {0x0000, 0x0002};
static const size_t VALID_CALL_FOR_RUN_COUNT = 2;

static const uint16_t VALID_FRAME_DELIM = 0x7530;  // bytes "75 30" as one big-endian value

// Only 2 zones are physically installed (Section 1), so bits 2-7 of the
// zone status bitmap should never be set.
static const uint8_t VALID_ZONE_STATUS[] = {0x00, ZONE1_BIT, ZONE2_BIT, static_cast<uint8_t>(ZONE1_BIT | ZONE2_BIT)};
static const size_t VALID_ZONE_STATUS_COUNT = 4;

// ---------------------------------------------------------------------------
// CRC-16/Modbus
// ---------------------------------------------------------------------------
// Poly 0xA001 (reflected 0x8005), init 0xFFFF, no xor-out, result transmitted
// little-endian (low byte first) per design document.
uint16_t crc16_modbus(const uint8_t *data, size_t length);

}  // namespace actron_classic
}  // namespace esphome
