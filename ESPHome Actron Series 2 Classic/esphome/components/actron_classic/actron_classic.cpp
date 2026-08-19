#include "actron_classic.h"
#include "climate/actron_climate.h"
#include "switch/actron_zone_switch.h"
#include "select/actron_fan_mode_select.h"
#include "select/actron_fan_speed_select.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstring>
#include <cinttypes>

namespace esphome {
namespace actron_classic {

static const char *const TAG = "actron_classic";

// ---------------------------------------------------------------------------
// Component lifecycle
// ---------------------------------------------------------------------------

void ActronClassic::setup() {
  if (this->de_re_pin_ != nullptr) {
    this->de_re_pin_->setup();
    this->set_bus_direction_write_(false);  // start in receive mode
  }
  this->last_valid_pl1_time_ = millis();
}

void ActronClassic::dump_config() {
  ESP_LOGCONFIG(TAG, "Actron Series 2 Classic Interface:");
  ESP_LOGCONFIG(TAG, "  Checksum failures: %" PRIu32, this->checksum_fail_count_);
  ESP_LOGCONFIG(TAG, "  Poll responses sent: %" PRIu32, this->poll_response_count_);
  LOG_PIN("  DE/RE Pin: ", this->de_re_pin_);
}

void ActronClassic::loop() {
  while (this->available()) {
    uint8_t b;
    if (this->read_byte(&b)) {
      this->feed_byte_(b);
    }
  }

  // Stale-bus watchdog (user-confirmed: 10s with no valid PL1 => stale).
  uint32_t age_ms = millis() - this->last_valid_pl1_time_;
  bool stale_now = (!this->has_valid_pl1_) || (age_ms > STALE_PL1_TIMEOUT_MS);
  if (stale_now != this->bus_stale_) {
    this->bus_stale_ = stale_now;
    if (this->bus_stale_sensor_ != nullptr) this->bus_stale_sensor_->publish_state(stale_now);
    if (stale_now) {
      ESP_LOGW(TAG, "No valid PL1 received in over %" PRIu32 " ms -- marking bus data stale", STALE_PL1_TIMEOUT_MS);
    } else {
      ESP_LOGI(TAG, "Bus data no longer stale");
    }
  }
  if (this->diagnostics_enabled_ && this->seconds_since_pl1_sensor_ != nullptr) {
    this->seconds_since_pl1_sensor_->publish_state(age_ms / 1000.0f);
  }
}

// ---------------------------------------------------------------------------
// Byte-level framing
// ---------------------------------------------------------------------------

void ActronClassic::push_header_window_(uint8_t b) {
  if (this->header_window_len_ < 8) {
    this->header_window_[this->header_window_len_++] = b;
  } else {
    memmove(this->header_window_, this->header_window_ + 1, 7);
    this->header_window_[7] = b;
  }
}

bool ActronClassic::header_window_matches_(const uint8_t *pattern) const {
  if (this->header_window_len_ < 8) return false;
  return memcmp(this->header_window_, pattern, 8) == 0;
}

void ActronClassic::feed_byte_(uint8_t b) {
  if (this->state_ == ParseState::READING_PL1) {
    this->rx_buf_[this->rx_len_++] = b;
    if (this->rx_len_ >= PL1_LEN) {
      this->process_pl1_frame_(this->rx_buf_);
      this->state_ = ParseState::IDLE;
      this->header_window_len_ = 0;
    }
    return;
  }

  // IDLE: look for a known 8-byte pattern at the tail of the rolling window.
  this->push_header_window_(b);

  if (this->header_window_matches_(PL1_HEADER)) {
    memcpy(this->rx_buf_, this->header_window_, 8);
    this->rx_len_ = 8;
    this->state_ = ParseState::READING_PL1;
    return;
  }
  if (this->header_window_matches_(POLL_REMOTE_67)) {
    this->handle_poll_(POLL_REMOTE_67);
    this->header_window_len_ = 0;
    return;
  }
  if (this->header_window_matches_(POLL_REMOTE_66) || this->header_window_matches_(POLL_REMOTE_68)) {
    // Not addressed to us -- a real (66) or absent (68) Wall Remote replies.
    this->header_window_len_ = 0;
    return;
  }
}

void ActronClassic::handle_poll_(const uint8_t *pattern) {
  this->last_poll_seen_ms_ = millis();
  this->send_pl4_();
  uint32_t latency = millis() - this->last_poll_seen_ms_;
  this->poll_response_count_++;
  if (this->diagnostics_enabled_ && this->poll_response_count_sensor_ != nullptr) {
    this->poll_response_count_sensor_->publish_state(this->poll_response_count_);
  }
  if (this->diagnostics_enabled_ && this->last_response_latency_sensor_ != nullptr) {
    this->last_response_latency_sensor_->publish_state(latency);
  }
  if (latency > POLL_RESPONSE_BUDGET_MS) {
    ESP_LOGW(TAG, "PL4 response took %" PRIu32 " ms, budget is %" PRIu32 " ms", latency, POLL_RESPONSE_BUDGET_MS);
  }
}

void ActronClassic::set_bus_direction_write_(bool write_mode) {
  if (this->de_re_pin_ != nullptr) {
    this->de_re_pin_->digital_write(write_mode);
  }
}

// ---------------------------------------------------------------------------
// PL1 handling
// ---------------------------------------------------------------------------

void ActronClassic::process_pl1_frame_(const uint8_t *buf) {
  uint16_t computed = crc16_modbus(buf, PL1_LEN - 2);
  uint16_t received = static_cast<uint16_t>(buf[PL1_LEN - 2]) | (static_cast<uint16_t>(buf[PL1_LEN - 1]) << 8);

  if (computed != received) {
    this->checksum_fail_count_++;
    if (this->diagnostics_enabled_ && this->checksum_fail_sensor_ != nullptr)
      this->checksum_fail_sensor_->publish_state(this->checksum_fail_count_);
    ESP_LOGD(TAG, "PL1 checksum mismatch (got 0x%04X, expected 0x%04X) -- discarding frame", received, computed);
    return;  // discard, wait for next valid PL1 (per design document)
  }

  memcpy(this->last_pl1_, buf, PL1_LEN);
  this->has_valid_pl1_ = true;
  this->last_valid_pl1_time_ = millis();

  this->validate_pl1_fields_(this->last_pl1_);
  this->check_action_verification_(this->last_pl1_);
  this->decode_and_publish_pl1_();
}

void ActronClassic::validate_pl1_fields_(const uint8_t *buf) {
  bool any_invalid = false;
  std::string detail;

  auto check_byte = [&](const char *name, size_t off, const uint8_t *valid, size_t valid_count) {
    uint8_t v = buf[off];
    for (size_t i = 0; i < valid_count; i++) {
      if (v == valid[i]) return;
    }
    any_invalid = true;
    char msg[48];
    snprintf(msg, sizeof(msg), "%s byte %u=0x%02X unexpected", name, static_cast<unsigned>(off), v);
    if (!detail.empty()) detail += "; ";
    detail += msg;
  };

  auto check_word = [&](const char *name, size_t off, uint16_t expected) {
    uint16_t v = (static_cast<uint16_t>(buf[off]) << 8) | buf[off + 1];
    if (v == expected) return;
    any_invalid = true;
    char msg[48];
    snprintf(msg, sizeof(msg), "%s bytes %u-%u=0x%04X unexpected", name, static_cast<unsigned>(off),
              static_cast<unsigned>(off + 1), v);
    if (!detail.empty()) detail += "; ";
    detail += msg;
  };

  auto check_word_set = [&](const char *name, size_t off, const uint16_t *valid, size_t valid_count) {
    uint16_t v = (static_cast<uint16_t>(buf[off]) << 8) | buf[off + 1];
    for (size_t i = 0; i < valid_count; i++) {
      if (v == valid[i]) return;
    }
    any_invalid = true;
    char msg[48];
    snprintf(msg, sizeof(msg), "%s bytes %u-%u=0x%04X unexpected", name, static_cast<unsigned>(off),
              static_cast<unsigned>(off + 1), v);
    if (!detail.empty()) detail += "; ";
    detail += msg;
  };

  check_byte("system status", PL1_OFF_SYSTEM_STATUS, VALID_SYSTEM_STATUS, VALID_SYSTEM_STATUS_COUNT);
  check_byte("system mode", PL1_OFF_SYSTEM_MODE, VALID_SYSTEM_MODE, VALID_SYSTEM_MODE_COUNT);
  check_byte("fan mode/speed", PL1_OFF_FAN_MODE_SPEED, VALID_FAN_MODE_SPEED, VALID_FAN_MODE_SPEED_COUNT);
  check_word("frame delim 1", PL1_OFF_FRAME_DELIM_1, VALID_FRAME_DELIM);
  check_byte("hw status reserved", PL1_OFF_HW_STATUS_RESERVED, VALID_HW_STATUS_RESERVED, VALID_HW_STATUS_RESERVED_COUNT);
  check_byte("hw status", PL1_OFF_HW_STATUS, VALID_HW_STATUS, VALID_HW_STATUS_COUNT);
  check_word_set("call for run", PL1_OFF_CALL_FOR_RUN, VALID_CALL_FOR_RUN, VALID_CALL_FOR_RUN_COUNT);
  check_word("frame delim 2", PL1_OFF_FRAME_DELIM_2, VALID_FRAME_DELIM);
  check_byte("zone status", PL1_OFF_ZONE_STATUS, VALID_ZONE_STATUS, VALID_ZONE_STATUS_COUNT);

  if (any_invalid) {
    this->validation_failure_count_++;
    ESP_LOGW(TAG, "PL1 payload validation failed: %s", detail.c_str());
  }

  if (!this->diagnostics_enabled_) return;
  if (this->validation_failures_sensor_ != nullptr && any_invalid) {
    this->validation_failures_sensor_->publish_state(this->validation_failure_count_);
  }
  if (this->validation_detail_sensor_ != nullptr && any_invalid) {
    this->validation_detail_sensor_->publish_state(detail);
  }
}

bool ActronClassic::verify_action_(const InFlightAction &a, const uint8_t *pl1) const {
  switch (a.action.type) {
    case ActronActionType::MODE:
      return pl1[PL1_OFF_SYSTEM_MODE] == a.action.u8_value;
    case ActronActionType::FAN:
      return pl1[PL1_OFF_FAN_MODE_SPEED] == a.action.u8_value;
    case ActronActionType::TEMP: {
      int16_t raw = (static_cast<int16_t>(pl1[PL1_OFF_SETPOINT]) << 8) | pl1[PL1_OFF_SETPOINT + 1];
      float actual = raw / 10.0f;
      return std::fabs(actual - a.action.f_value) < 0.05f;
    }
    case ActronActionType::POWER: {
      uint8_t expected = a.action.u8_value ? SYSTEM_STATUS_ON : SYSTEM_STATUS_OFF;
      return pl1[PL1_OFF_SYSTEM_STATUS] == expected;
    }
    case ActronActionType::ZONE1:
      return static_cast<bool>(pl1[PL1_OFF_ZONE_STATUS] & ZONE1_BIT) == static_cast<bool>(a.action.u8_value);
    case ActronActionType::ZONE2:
      return static_cast<bool>(pl1[PL1_OFF_ZONE_STATUS] & ZONE2_BIT) == static_cast<bool>(a.action.u8_value);
  }
  return true;
}

void ActronClassic::revert_action_entity_(const ActronAction &a) {
  // Re-publish whatever last_pl1_ actually says for this action's entity,
  // undoing the optimistic UI update. decode_and_publish_pl1_() normally
  // skips entities with an active in-flight action; since we've just
  // cleared this one, calling it again will now sync this entity's value.
  this->decode_and_publish_pl1_();
}

void ActronClassic::mark_action_confirmed_(const ActronAction &a) {
  // No-op: the next decode_and_publish_pl1_() pass will publish the (now
  // matching) real value once the in-flight slot is cleared.
}

void ActronClassic::check_action_verification_(const uint8_t *buf) {
  for (size_t i = 0; i < MAX_IN_FLIGHT_ACTIONS; i++) {
    InFlightAction &f = this->in_flight_[i];
    if (!f.active) continue;

    if (this->verify_action_(f, buf)) {
      ESP_LOGD(TAG, "Action confirmed after %u attempt(s)", f.attempts);
      this->mark_action_confirmed_(f.action);
      f.active = false;
      continue;
    }

    f.poll_cycles_waited++;
    if (f.poll_cycles_waited >= MAX_VERIFY_CYCLES) {
      if (f.attempts < MAX_ACTION_ATTEMPTS) {
        ESP_LOGD(TAG, "Action not reflected in PL1 after %u cycles, retrying (attempt %u/%u)", MAX_VERIFY_CYCLES,
                 f.attempts + 1, MAX_ACTION_ATTEMPTS);
        f.poll_cycles_waited = 0;
        this->action_retry_count_++;
        if (this->diagnostics_enabled_ && this->action_retry_count_sensor_ != nullptr)
          this->action_retry_count_sensor_->publish_state(this->action_retry_count_);
        // stays active; will be re-embedded and re-sent on the next PL4
      } else {
        ESP_LOGW(TAG, "Action failed verification after %u attempts, reverting entity to actual PL1 state",
                 f.attempts);
        this->revert_action_entity_(f.action);
        f.active = false;
      }
    }
  }

  if (this->diagnostics_enabled_ && this->action_queue_depth_sensor_ != nullptr) {
    size_t in_flight_count = 0;
    for (auto &f : this->in_flight_)
      if (f.active) in_flight_count++;
    this->action_queue_depth_sensor_->publish_state(this->action_queue_.size() + in_flight_count);
  }
}

// ---------------------------------------------------------------------------
// Decoding + publishing entities
// ---------------------------------------------------------------------------

static bool nibble_high_(uint8_t b, uint8_t v) { return ((b >> 4) & 0x0F) == v; }
static bool nibble_low_(uint8_t b, uint8_t v) { return (b & 0x0F) == v; }

bool ActronClassic::zone_on(uint8_t zone_number) const {
  uint8_t mask = (zone_number == 1) ? ZONE1_BIT : ZONE2_BIT;
  return (this->last_pl1_[PL1_OFF_ZONE_STATUS] & mask) != 0;
}

float ActronClassic::setpoint_c() const {
  int16_t raw = (static_cast<int16_t>(this->last_pl1_[PL1_OFF_SETPOINT]) << 8) | this->last_pl1_[PL1_OFF_SETPOINT + 1];
  return raw / 10.0f;
}

bool ActronClassic::fan_byte_is_continuous_(uint8_t fan_byte) { return (fan_byte & FAN_CONTINUOUS_BIT) != 0; }

const char *ActronClassic::fan_speed_to_str_(uint8_t fan_byte) {
  switch (fan_byte & FAN_SPEED_MASK) {
    case FAN_LOW: return "Low";
    case FAN_MED: return "Med";
    case FAN_HIGH: return "High";
    default: return "Low";
  }
}

bool ActronClassic::has_pending_action_(ActronActionType t) const {
  for (size_t i = 0; i < MAX_IN_FLIGHT_ACTIONS; i++) {
    if (this->in_flight_[i].active && this->in_flight_[i].action.type == t) return true;
  }
  for (const auto &a : this->action_queue_) {
    if (a.type == t) return true;
  }
  return false;
}

void ActronClassic::decode_and_publish_pl1_() {
  const uint8_t *buf = this->last_pl1_;

  // Bug #4: these must also cover action_queue_
  // (not just in_flight_), otherwise a PL1 that arrives after HA requests a
  // change but before that action is promoted to in-flight on the next poll
  // cycle would overwrite the optimistic UI value, causing the entity to
  // jitter back and forth until the action is actually embedded in a PL4.
  bool has_mode_inflight = this->has_pending_action_(ActronActionType::MODE);
  bool has_fan_inflight = this->has_pending_action_(ActronActionType::FAN);
  bool has_temp_inflight = this->has_pending_action_(ActronActionType::TEMP);
  bool has_power_inflight = this->has_pending_action_(ActronActionType::POWER);
  bool has_zone1_inflight = this->has_pending_action_(ActronActionType::ZONE1);
  bool has_zone2_inflight = this->has_pending_action_(ActronActionType::ZONE2);

  bool on = buf[PL1_OFF_SYSTEM_STATUS] == SYSTEM_STATUS_ON;
  auto mode = static_cast<SystemMode>(buf[PL1_OFF_SYSTEM_MODE]);
  uint8_t fan_byte = buf[PL1_OFF_FAN_MODE_SPEED];
  float setpoint = this->setpoint_c();

  int16_t temp_actual_raw = (static_cast<int16_t>(buf[PL1_OFF_TEMP_ACTUAL]) << 8) | buf[PL1_OFF_TEMP_ACTUAL + 1];
  float temp_actual = temp_actual_raw / 10.0f;

  int16_t indoor_coil_raw =
      (static_cast<int16_t>(buf[PL1_OFF_INDOOR_COIL_TEMP]) << 8) | buf[PL1_OFF_INDOOR_COIL_TEMP + 1];
  float indoor_coil = indoor_coil_raw / 10.0f;

  int16_t outdoor_coil_raw =
      (static_cast<int16_t>(buf[PL1_OFF_OUTDOOR_COIL_TEMP]) << 8) | buf[PL1_OFF_OUTDOOR_COIL_TEMP + 1];
  float outdoor_coil = outdoor_coil_raw / 10.0f;

  // The source design document was corrected
  // -- Hardware Status is the byte 37-38 pair, not byte 37 alone. Byte 37 is
  // expected to always read 0x00; byte 38 carries the nibble encoding. See
  // PL1_OFF_HW_STATUS in protocol.h. Both nibbles are read directly from the
  // payload (not inferred from another field); validate_pl1_fields_() flags
  // it if either byte ever holds an unexpected value, to help catch a wrong
  // protocol assumption like this one going forward.
  uint8_t hw = buf[PL1_OFF_HW_STATUS];
  bool reversing = nibble_high_(hw, 0x1);      // 0x_0 nibble = Normal, 0x1_ = Reverse
  bool compressor_on = nibble_low_(hw, 0x8);   // 0x_0 = Off, 0x_8 = On

  uint16_t cfr = (static_cast<uint16_t>(buf[PL1_OFF_CALL_FOR_RUN]) << 8) | buf[PL1_OFF_CALL_FOR_RUN + 1];
  bool call_for_run = cfr == 0x0002;  // == "Run Request" (user-confirmed same signal)

  uint16_t compressor_raw =
      (static_cast<uint16_t>(buf[PL1_OFF_COMPRESSOR_SPEED]) << 8) | buf[PL1_OFF_COMPRESSOR_SPEED + 1];
  float compressor_pct = compressor_raw;  // 0x64 == 100, value already in percent

  uint16_t fan_rpm = (static_cast<uint16_t>(buf[PL1_OFF_INDOOR_FAN_RPM]) << 8) | buf[PL1_OFF_INDOOR_FAN_RPM + 1];

  bool zone1 = this->zone_on(1);
  bool zone2 = this->zone_on(2);

  // "System State (Idle, Running): Running if Fan speed is > 0 OR Call for
  // Run is TRUE." Interpreted as measured Indoor Fan RPM (the actual running
  // signal), not the user's selected fan speed setting -- see docs/PROTOCOL_REFERENCE.md.
  bool running = (fan_rpm > 50) || call_for_run;

  // --- climate ---
  if (this->climate_ != nullptr) {
    bool changed = false;
    climate::ClimateMode new_mode;
    if (!on) {
      new_mode = climate::CLIMATE_MODE_OFF;
    } else {
      switch (mode) {
        case MODE_COOL: new_mode = climate::CLIMATE_MODE_COOL; break;
        case MODE_HEAT: new_mode = climate::CLIMATE_MODE_HEAT; break;
        case MODE_FAN: new_mode = climate::CLIMATE_MODE_FAN_ONLY; break;
        case MODE_AUTO: new_mode = climate::CLIMATE_MODE_HEAT_COOL; break;
        default: new_mode = climate::CLIMATE_MODE_OFF; break;
      }
    }
    if (!has_power_inflight && !has_mode_inflight) {
      if (this->climate_->mode != new_mode) { this->climate_->mode = new_mode; changed = true; }
    }
    if (!has_temp_inflight) {
      if (this->climate_->target_temperature != setpoint) { this->climate_->target_temperature = setpoint; changed = true; }
    }
    if (this->climate_->current_temperature != temp_actual) { this->climate_->current_temperature = temp_actual; changed = true; }

    climate::ClimateAction action = climate::CLIMATE_ACTION_OFF;
    if (on) {
      if (mode == MODE_FAN) action = climate::CLIMATE_ACTION_FAN;
      else if (compressor_on && mode == MODE_COOL) action = climate::CLIMATE_ACTION_COOLING;
      else if (compressor_on && mode == MODE_HEAT) action = climate::CLIMATE_ACTION_HEATING;
      else if (compressor_on && mode == MODE_AUTO) action = reversing ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_COOLING;
      else action = climate::CLIMATE_ACTION_IDLE;
    }
    if (this->climate_->action != action) { this->climate_->action = action; changed = true; }

    if (changed) this->climate_->publish_state();
  }

  // --- fan mode / speed selects ---
  this->current_fan_mode_str_ = fan_byte_is_continuous_(fan_byte) ? "Continuous" : "Normal";
  this->current_fan_speed_str_ = fan_speed_to_str_(fan_byte);
  this->fan_state_known_ = true;
  if (!has_fan_inflight) {
    if (this->fan_mode_select_ != nullptr) this->fan_mode_select_->publish_state(this->current_fan_mode_str_);
    if (this->fan_speed_select_ != nullptr) this->fan_speed_select_->publish_state(this->current_fan_speed_str_);
  }

  // --- zone switches / status ---
  // Note: the *_switch_ entities are the actionable main-category switches
  // and always publish; only the *_status_sensor_ diagnostic duplicates are
  // gated by diagnostics_enabled_.
  if (!has_zone1_inflight) {
    if (this->zone1_switch_ != nullptr) this->zone1_switch_->publish_state(zone1);
    if (this->diagnostics_enabled_ && this->zone1_status_sensor_ != nullptr) this->zone1_status_sensor_->publish_state(zone1);
  }
  if (!has_zone2_inflight) {
    if (this->zone2_switch_ != nullptr) this->zone2_switch_->publish_state(zone2);
    if (this->diagnostics_enabled_ && this->zone2_status_sensor_ != nullptr) this->zone2_status_sensor_->publish_state(zone2);
  }

  // --- power / running binary sensors ---
  if (!has_power_inflight && this->system_power_sensor_ != nullptr) this->system_power_sensor_->publish_state(on);
  if (this->system_running_sensor_ != nullptr) this->system_running_sensor_->publish_state(running);
  if (this->diagnostics_enabled_ && this->reversing_valve_sensor_ != nullptr)
    this->reversing_valve_sensor_->publish_state(reversing);
  if (this->diagnostics_enabled_ && this->compressor_contactor_sensor_ != nullptr)
    this->compressor_contactor_sensor_->publish_state(compressor_on);
  if (this->diagnostics_enabled_ && this->run_request_sensor_ != nullptr)
    this->run_request_sensor_->publish_state(call_for_run);

  // --- system clock (diagnostic only, not user-actionable) ---
  // Only publish once an hour.
  uint32_t now_ms = millis();
  bool clock_due = !this->clock_published_once_ || (now_ms - this->last_clock_publish_ms_) >= SYSTEM_CLOCK_PUBLISH_INTERVAL_MS;
  if (this->diagnostics_enabled_ && this->system_clock_sensor_ != nullptr && clock_due) {
    // Sized for the worst case ("255:255:255\0") since buf[] entries are
    // uint8_t (0-255) as far as the compiler can prove, even though the
    // clock bytes are always valid HH/MM/SS (0-23 / 0-59 / 0-59) in practice.
    char buf_str[12];
    snprintf(buf_str, sizeof(buf_str), "%02u:%02u:%02u", buf[PL1_OFF_CLOCK], buf[PL1_OFF_CLOCK + 1], buf[PL1_OFF_CLOCK + 2]);
    this->system_clock_sensor_->publish_state(buf_str);
    this->last_clock_publish_ms_ = now_ms;
    this->clock_published_once_ = true;
  }

  // --- debounced telemetry ---
  if (this->temp_actual_debounce_.update(temp_actual)) {
    // current_temperature already published above unconditionally since it
    // also drives the climate card; nothing else needs debounce for it.
  }
  if (this->indoor_coil_debounce_.update(indoor_coil) && this->indoor_coil_temp_sensor_ != nullptr) {
    this->indoor_coil_temp_sensor_->publish_state(indoor_coil);
  }
  if (this->outdoor_coil_debounce_.update(outdoor_coil) && this->outdoor_coil_temp_sensor_ != nullptr) {
    this->outdoor_coil_temp_sensor_->publish_state(outdoor_coil);
  }
  if (this->compressor_speed_debounce_.update(compressor_pct) && this->compressor_speed_sensor_ != nullptr) {
    this->compressor_speed_sensor_->publish_state(compressor_pct);
  }
  if (this->indoor_fan_rpm_debounce_.update(fan_rpm) && this->indoor_fan_rpm_sensor_ != nullptr) {
    this->indoor_fan_rpm_sensor_->publish_state(fan_rpm);
  }
}

// ---------------------------------------------------------------------------
// PL4 construction + transmission
// ---------------------------------------------------------------------------

void ActronClassic::fill_in_flight_from_queue_() {
  for (size_t i = 0; i < MAX_IN_FLIGHT_ACTIONS; i++) {
    if (this->in_flight_[i].active) continue;
    if (this->action_queue_.empty()) break;
    this->in_flight_[i].action = this->action_queue_.front();
    this->action_queue_.pop_front();
    this->in_flight_[i].active = true;
    this->in_flight_[i].attempts = 0;
    this->in_flight_[i].poll_cycles_waited = 0;
  }
}

uint8_t ActronClassic::build_action_byte_and_bytes_(uint8_t *pl4) {
  uint8_t action_byte = 0;
  for (size_t i = 0; i < MAX_IN_FLIGHT_ACTIONS; i++) {
    InFlightAction &f = this->in_flight_[i];
    if (!f.active) continue;

    action_byte += f.action.byte14_bit;
    f.attempts++;

    switch (f.action.type) {
      case ActronActionType::MODE:
        pl4[PL4_OFF_MODE_SETTING] = f.action.u8_value;
        break;
      case ActronActionType::FAN:
        pl4[PL4_OFF_FAN_SETTING] = f.action.u8_value;
        break;
      case ActronActionType::TEMP: {
        uint16_t raw = static_cast<uint16_t>(std::lround(f.action.f_value * 10.0f));
        pl4[PL4_OFF_TEMP_SETTING_HI] = static_cast<uint8_t>(raw >> 8);
        pl4[PL4_OFF_TEMP_SETTING_LO] = static_cast<uint8_t>(raw & 0xFF);
        break;
      }
      case ActronActionType::POWER:
        // Power must update BOTH byte 5 and byte 6, not byte 5 alone --
        //   On -> Off:  byte 5 = 0x02 (Off), byte 6 = 0x00 (MODE_OFF)
        //   Off -> On:  byte 5 = 0x00 (On),  byte 6 = the mode being entered
        // (this was previously a suspected root cause of "Off doesn't turn
        // off the system": byte 6 was left as an unchanged duplicate of the
        // last PL1 mode, so the controller had nothing telling it the mode
        // setting had actually been actioned alongside the power setting).
        if (f.action.u8_value) {
          pl4[PL4_OFF_POWER_SETTING] = SYSTEM_STATUS_ON;
          pl4[PL4_OFF_MODE_SETTING] = static_cast<uint8_t>(f.action.power_on_mode);
        } else {
          pl4[PL4_OFF_POWER_SETTING] = SYSTEM_STATUS_OFF;
          pl4[PL4_OFF_MODE_SETTING] = MODE_OFF;
        }
        break;
      case ActronActionType::ZONE1:
        if (f.action.u8_value) pl4[PL4_OFF_ZONE_SETTING] |= ZONE1_BIT;
        else pl4[PL4_OFF_ZONE_SETTING] &= ~ZONE1_BIT;
        break;
      case ActronActionType::ZONE2:
        if (f.action.u8_value) pl4[PL4_OFF_ZONE_SETTING] |= ZONE2_BIT;
        else pl4[PL4_OFF_ZONE_SETTING] &= ~ZONE2_BIT;
        break;
    }
  }
  return action_byte;
}

void ActronClassic::build_pl4_(uint8_t *out) {
  memset(out, 0, PL4_LEN);
  memcpy(out + PL4_OFF_STATIC_HEADER, PL4_STATIC_HEADER, sizeof(PL4_STATIC_HEADER));

  if (this->has_valid_pl1_) {
    for (size_t i = 0; i < PL4_DUP_SEGMENT_COUNT; i++) {
      const Pl4DupSegment &seg = PL4_DUP_SEGMENTS[i];
      memcpy(out + seg.pl4_offset, this->last_pl1_ + seg.pl1_offset, seg.length);
    }
  }

  memcpy(out + PL4_OFF_00FF_FF, PL4_STATIC_00FF_FF, sizeof(PL4_STATIC_00FF_FF));

  this->fill_in_flight_from_queue_();
  out[PL4_OFF_ACTION_BYTE] = this->build_action_byte_and_bytes_(out);

  uint16_t crc = crc16_modbus(out, PL4_OFF_CHECKSUM);
  out[PL4_OFF_CHECKSUM] = static_cast<uint8_t>(crc & 0xFF);
  out[PL4_OFF_CHECKSUM + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
}

void ActronClassic::send_pl4_() {
  uint8_t pl4[PL4_LEN];
  this->build_pl4_(pl4);

  this->set_bus_direction_write_(true);
  this->write_array(pl4, PL4_LEN);
  this->flush();
  this->set_bus_direction_write_(false);
}

// ---------------------------------------------------------------------------
// Action queueing (called from climate / switch / select platforms)
// ---------------------------------------------------------------------------

void ActronClassic::enqueue_action_(const ActronAction &action) {
  // Supersede an existing *queued* (not yet in-flight) action of the same type.
  for (auto &existing : this->action_queue_) {
    if (existing.type == action.type) {
      existing = action;
      return;
    }
  }
  if (this->action_queue_.size() >= MAX_QUEUED_ACTIONS) {
    ESP_LOGW(TAG, "Action queue full (%u), discarding new action and reverting entity to actual state",
             static_cast<unsigned>(MAX_QUEUED_ACTIONS));
    this->decode_and_publish_pl1_();
    return;
  }
  this->action_queue_.push_back(action);
}

void ActronClassic::queue_mode_action(SystemMode mode) {
  ActronAction a;
  a.type = ActronActionType::MODE;
  a.byte14_bit = ACTION_BIT_MODE;
  a.u8_value = static_cast<uint8_t>(mode);
  this->enqueue_action_(a);
}

void ActronClassic::queue_fan_action(FanModeSpeed fan) {
  ActronAction a;
  a.type = ActronActionType::FAN;
  a.byte14_bit = ACTION_BIT_FAN;
  a.u8_value = static_cast<uint8_t>(fan);
  this->enqueue_action_(a);
}

void ActronClassic::queue_temp_action(float setpoint_c) {
  float clamped = setpoint_c;
  if (clamped < SETPOINT_MIN_C) clamped = SETPOINT_MIN_C;
  if (clamped > SETPOINT_MAX_C) clamped = SETPOINT_MAX_C;
  // snap to 0.5 degC grid
  clamped = std::round(clamped / SETPOINT_STEP_C) * SETPOINT_STEP_C;

  ActronAction a;
  a.type = ActronActionType::TEMP;
  a.byte14_bit = ACTION_BIT_TEMP;
  a.f_value = clamped;
  this->enqueue_action_(a);
}

void ActronClassic::queue_power_action(bool on, SystemMode mode_if_on) {
  ActronAction a;
  a.type = ActronActionType::POWER;
  a.byte14_bit = ACTION_BIT_POWER;
  a.u8_value = on ? 1 : 0;
  a.power_on_mode = mode_if_on;
  this->enqueue_action_(a);
}

void ActronClassic::queue_zone_action(uint8_t zone_number, bool on) {
  ActronAction a;
  a.type = (zone_number == 1) ? ActronActionType::ZONE1 : ActronActionType::ZONE2;
  a.byte14_bit = ACTION_BIT_ZONE;
  a.u8_value = on ? 1 : 0;
  this->enqueue_action_(a);
}

void ActronClassic::request_hvac_mode(climate::ClimateMode mode) {
  bool want_on = mode != climate::CLIMATE_MODE_OFF;
  bool currently_on = this->has_valid_pl1_ ? this->system_on() : false;

  if (want_on) {
    SystemMode desired;
    switch (mode) {
      case climate::CLIMATE_MODE_COOL: desired = MODE_COOL; break;
      case climate::CLIMATE_MODE_HEAT: desired = MODE_HEAT; break;
      case climate::CLIMATE_MODE_FAN_ONLY: desired = MODE_FAN; break;
      case climate::CLIMATE_MODE_HEAT_COOL: desired = MODE_AUTO; break;
      default: return;
    }
    if (!currently_on) {
      // Off -> On: the Power action alone carries the target mode via PL4
      // byte 6 (see the PL4_OFF_POWER_SETTING/PL4_OFF_MODE_SETTING
      // correction note in protocol.h). Deliberately NOT also queuing a
      // separate Mode action here and returning immediately: byte 14 is the
      // *sum* of every simultaneously in-flight action's bit value
      // (build_action_byte_and_bytes_() does `action_byte += ...` across
      // both in-flight slots), so Power (0x07) queued together with Mode
      // (0x01) would promote into the same PL4 frame as action byte 0x08 --
      // not one of the controller's five documented button codes, so the
      // unit doesn't act on it. This was diagnosed as the reason "On" did
      // nothing from Home Assistant even though "Off" worked (Off only ever
      // queues the single Power action).
      this->queue_power_action(true, desired);
      return;
    }
    bool mode_already_correct = this->has_valid_pl1_ && (this->system_mode() == desired);
    if (!mode_already_correct) {
      this->queue_mode_action(desired);
    }
  } else if (currently_on) {
    this->queue_power_action(false);
  }
}

FanModeSpeed ActronClassic::combine_fan_selection_() const {
  bool continuous = this->current_fan_mode_str_ == "Continuous";
  uint8_t speed_bits;
  if (this->current_fan_speed_str_ == "Med") speed_bits = FAN_MED;
  else if (this->current_fan_speed_str_ == "High") speed_bits = FAN_HIGH;
  else speed_bits = FAN_LOW;
  return static_cast<FanModeSpeed>(continuous ? (speed_bits | FAN_CONTINUOUS_BIT) : speed_bits);
}

void ActronClassic::on_fan_mode_selected(const std::string &mode_str) {
  this->current_fan_mode_str_ = mode_str;
  this->queue_fan_action(this->combine_fan_selection_());
}

void ActronClassic::on_fan_speed_selected(const std::string &speed_str) {
  this->current_fan_speed_str_ = speed_str;
  this->queue_fan_action(this->combine_fan_selection_());
}

}  // namespace actron_classic
}  // namespace esphome
