#pragma once

#include <deque>
#include <string>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/climate/climate_mode.h"
#include "protocol.h"

namespace esphome {
namespace actron_classic {

// Forward declare to avoid a circular include; climate/switch/select
// platforms include actron_classic.h and register themselves here.
class ActronClimate;
class ActronZoneSwitch;
class ActronDiagnosticsSwitch;
class ActronFanModeSelect;
class ActronFanSpeedSelect;

enum class ActronActionType : uint8_t {
  MODE,
  FAN,
  TEMP,
  POWER,
  ZONE1,
  ZONE2,
};

struct ActronAction {
  ActronActionType type;
  uint8_t byte14_bit{0};
  uint8_t u8_value{0};   // MODE -> SystemMode, FAN -> FanModeSpeed, POWER/ZONE1/ZONE2 -> 0/1
  float f_value{0};      // TEMP -> target setpoint in degC
  // POWER only, used when u8_value=1 (Off->On): the mode PL4 byte 6 should
  // be set to alongside byte 5, per the corrected design document (Claude
  // Feedback, 2026-08-15) -- "Off > On: Change byte 5 to 0x00, change byte 6
  // to the value corresponding to the mode being entered into."
  SystemMode power_on_mode{MODE_COOL};
};

struct InFlightAction {
  ActronAction action;
  bool active{false};
  uint8_t attempts{0};          // number of times this action has been transmitted
  uint8_t poll_cycles_waited{0};  // consecutive verification-checking PL1s since last (re)send
};

class ActronClassic : public Component, public uart::UARTDevice {
 public:
  static const size_t MAX_IN_FLIGHT_ACTIONS = 2;
  static const size_t MAX_QUEUED_ACTIONS = 10;
  static const uint8_t MAX_VERIFY_CYCLES = 3;   // PL1 cycles to wait before retrying
  static const uint8_t MAX_ACTION_ATTEMPTS = 3;  // total transmissions before giving up

  void set_de_re_pin(GPIOPin *pin) { this->de_re_pin_ = pin; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  // --- entity registration (called from platform __init__ codegen) ---
  void set_climate(ActronClimate *c) { this->climate_ = c; }
  void set_fan_mode_select(ActronFanModeSelect *s) { this->fan_mode_select_ = s; }
  void set_fan_speed_select(ActronFanSpeedSelect *s) { this->fan_speed_select_ = s; }
  void set_zone1_switch(ActronZoneSwitch *s) { this->zone1_switch_ = s; }
  void set_zone2_switch(ActronZoneSwitch *s) { this->zone2_switch_ = s; }

  // Called from ActronDiagnosticsSwitch::write_state(). Gates whether the
  // diagnostic-category entities (bus/action counters + reversing valve,
  // compressor contactor, run request, zone status, system clock) publish
  // updates to Home Assistant; their underlying counters/state keep tracking
  // internally either way. Defaults to true (unchanged existing behaviour).
  void set_diagnostics_enabled(bool enabled) { this->diagnostics_enabled_ = enabled; }

  void set_indoor_coil_temp_sensor(sensor::Sensor *s) { this->indoor_coil_temp_sensor_ = s; }
  void set_outdoor_coil_temp_sensor(sensor::Sensor *s) { this->outdoor_coil_temp_sensor_ = s; }
  void set_compressor_speed_sensor(sensor::Sensor *s) { this->compressor_speed_sensor_ = s; }
  void set_indoor_fan_rpm_sensor(sensor::Sensor *s) { this->indoor_fan_rpm_sensor_ = s; }

  void set_reversing_valve_binary_sensor(binary_sensor::BinarySensor *s) { this->reversing_valve_sensor_ = s; }
  void set_compressor_contactor_binary_sensor(binary_sensor::BinarySensor *s) { this->compressor_contactor_sensor_ = s; }
  void set_run_request_binary_sensor(binary_sensor::BinarySensor *s) { this->run_request_sensor_ = s; }
  void set_system_power_binary_sensor(binary_sensor::BinarySensor *s) { this->system_power_sensor_ = s; }
  void set_system_running_binary_sensor(binary_sensor::BinarySensor *s) { this->system_running_sensor_ = s; }
  void set_zone1_status_binary_sensor(binary_sensor::BinarySensor *s) { this->zone1_status_sensor_ = s; }
  void set_zone2_status_binary_sensor(binary_sensor::BinarySensor *s) { this->zone2_status_sensor_ = s; }
  void set_bus_stale_binary_sensor(binary_sensor::BinarySensor *s) { this->bus_stale_sensor_ = s; }

  void set_system_clock_text_sensor(text_sensor::TextSensor *s) { this->system_clock_sensor_ = s; }

  // diagnostics
  void set_checksum_fail_sensor(sensor::Sensor *s) { this->checksum_fail_sensor_ = s; }
  void set_poll_response_count_sensor(sensor::Sensor *s) { this->poll_response_count_sensor_ = s; }
  void set_last_response_latency_sensor(sensor::Sensor *s) { this->last_response_latency_sensor_ = s; }
  void set_action_queue_depth_sensor(sensor::Sensor *s) { this->action_queue_depth_sensor_ = s; }
  void set_action_retry_count_sensor(sensor::Sensor *s) { this->action_retry_count_sensor_ = s; }
  void set_seconds_since_pl1_sensor(sensor::Sensor *s) { this->seconds_since_pl1_sensor_ = s; }

  // Claude Feedback (2026-08-15): surfaces when a monitored PL1 byte holds a
  // value outside its documented expected set (e.g. the byte 37/38 hardware
  // status field), to help diagnose protocol assumptions that turn out to be
  // wrong instead of silently mis-decoding them. See validate_pl1_fields_().
  void set_validation_failures_sensor(sensor::Sensor *s) { this->validation_failures_sensor_ = s; }
  void set_validation_detail_text_sensor(text_sensor::TextSensor *s) { this->validation_detail_sensor_ = s; }

  // --- called by climate/switch/select platforms to request bus actions ---
  void queue_mode_action(SystemMode mode);
  void queue_fan_action(FanModeSpeed fan);
  void queue_temp_action(float setpoint_c);
  // mode_if_on only matters when turning the system on (Off->On); see
  // ActronAction::power_on_mode.
  void queue_power_action(bool on, SystemMode mode_if_on = MODE_COOL);
  void queue_zone_action(uint8_t zone_number, bool on);  // zone_number: 1 or 2

  // Called from ActronClimate::control(). Diffs the requested HVAC mode
  // against the last known real state and queues only the actions that are
  // actually necessary (mirrors the worked example in the design document).
  void request_hvac_mode(climate::ClimateMode mode);

  // Called from the fan mode / fan speed select platforms. Each stores the
  // user's last selection and recomputes the combined Actron fan byte.
  void on_fan_mode_selected(const std::string &mode_str);    // "Normal" / "Continuous"
  void on_fan_speed_selected(const std::string &speed_str);  // "Low" / "Med" / "High"

  // read-only snapshot accessors used by platforms to compute "necessary
  // action only" diffs (control() implementations check current PL1 state
  // before deciding whether an action is actually needed).
  bool has_valid_pl1() const { return this->has_valid_pl1_; }
  bool system_on() const { return this->last_pl1_[PL1_OFF_SYSTEM_STATUS] == SYSTEM_STATUS_ON; }
  SystemMode system_mode() const { return static_cast<SystemMode>(this->last_pl1_[PL1_OFF_SYSTEM_MODE]); }
  uint8_t fan_mode_speed_byte() const { return this->last_pl1_[PL1_OFF_FAN_MODE_SPEED]; }
  float setpoint_c() const;
  bool zone_on(uint8_t zone_number) const;

 protected:
  // --- UART framing ---
  enum class ParseState { IDLE, READING_PL1 };
  ParseState state_{ParseState::IDLE};
  uint8_t header_window_[8]{0};
  uint8_t header_window_len_{0};
  uint8_t rx_buf_[PL1_LEN]{0};
  size_t rx_len_{0};

  void feed_byte_(uint8_t b);
  void push_header_window_(uint8_t b);
  bool header_window_matches_(const uint8_t *pattern) const;

  void process_pl1_frame_(const uint8_t *buf);
  void handle_poll_(const uint8_t *pattern);
  void send_pl4_();
  void build_pl4_(uint8_t *out);
  void set_bus_direction_write_(bool write_mode);

  void decode_and_publish_pl1_();
  void check_action_verification_(const uint8_t *buf);
  void fill_in_flight_from_queue_();
  uint8_t build_action_byte_and_bytes_(uint8_t *pl4);
  bool verify_action_(const InFlightAction &a, const uint8_t *pl1) const;
  void revert_action_entity_(const ActronAction &a);
  void mark_action_confirmed_(const ActronAction &a);
  void enqueue_action_(const ActronAction &action);

  // True if an action of type `t` is either already in flight or still
  // sitting in action_queue_ waiting to be embedded into the next PL4. Used
  // by decode_and_publish_pl1_() to decide whether to skip publishing an
  // entity's PL1-derived value. Checking action_queue_ too (not just
  // in_flight_) closes the race from Claude Feedback (2026-08-15) bug #4:
  // previously a PL1 arriving between an action being queued and it being
  // promoted to in-flight (next poll cycle) would overwrite the optimistic
  // HA state, causing the reported jitter.
  bool has_pending_action_(ActronActionType t) const;

  // Checks every PL1 byte/byte-pair we treat as an enumerated (not
  // continuous) field against its documented set of valid values -- system
  // status, system mode, fan mode/speed, the two "75 30" frame delimiters,
  // hardware status (byte 37/38), call-for-run, and zone status. On a
  // mismatch, increments validation_failure_count_ and records a
  // human-readable description for validation_detail_sensor_. Claude
  // Feedback (2026-08-15): added after the byte 37/38 hardware-status field
  // turned out to be misread against an updated design document, to catch
  // similar wrong assumptions going forward instead of silently mis-decoding.
  void validate_pl1_fields_(const uint8_t *buf);

  GPIOPin *de_re_pin_{nullptr};

  uint8_t last_pl1_[PL1_LEN]{0};
  bool has_valid_pl1_{false};
  uint32_t last_valid_pl1_time_{0};
  bool bus_stale_{true};

  std::deque<ActronAction> action_queue_;
  InFlightAction in_flight_[MAX_IN_FLIGHT_ACTIONS];

  // debounce state for telemetry-only fields
  template<typename T> struct Debounce {
    T last_raw{};
    uint8_t count{0};
    bool has_value{false};
    bool update(T new_raw) {
      if (this->has_value && new_raw == this->last_raw) {
        if (this->count < TELEMETRY_DEBOUNCE_COUNT) this->count++;
      } else {
        this->last_raw = new_raw;
        this->count = 1;
        this->has_value = true;
      }
      return this->count >= TELEMETRY_DEBOUNCE_COUNT;
    }
  };
  Debounce<float> temp_actual_debounce_;
  Debounce<float> indoor_coil_debounce_;
  Debounce<float> outdoor_coil_debounce_;
  Debounce<float> compressor_speed_debounce_;
  Debounce<uint16_t> indoor_fan_rpm_debounce_;

  // entities
  ActronClimate *climate_{nullptr};
  ActronFanModeSelect *fan_mode_select_{nullptr};
  ActronFanSpeedSelect *fan_speed_select_{nullptr};
  ActronZoneSwitch *zone1_switch_{nullptr};
  ActronZoneSwitch *zone2_switch_{nullptr};
  bool diagnostics_enabled_{true};

  sensor::Sensor *indoor_coil_temp_sensor_{nullptr};
  sensor::Sensor *outdoor_coil_temp_sensor_{nullptr};
  sensor::Sensor *compressor_speed_sensor_{nullptr};
  sensor::Sensor *indoor_fan_rpm_sensor_{nullptr};

  binary_sensor::BinarySensor *reversing_valve_sensor_{nullptr};
  binary_sensor::BinarySensor *compressor_contactor_sensor_{nullptr};
  binary_sensor::BinarySensor *run_request_sensor_{nullptr};
  binary_sensor::BinarySensor *system_power_sensor_{nullptr};
  binary_sensor::BinarySensor *system_running_sensor_{nullptr};
  binary_sensor::BinarySensor *zone1_status_sensor_{nullptr};
  binary_sensor::BinarySensor *zone2_status_sensor_{nullptr};
  binary_sensor::BinarySensor *bus_stale_sensor_{nullptr};

  text_sensor::TextSensor *system_clock_sensor_{nullptr};

  sensor::Sensor *checksum_fail_sensor_{nullptr};
  sensor::Sensor *poll_response_count_sensor_{nullptr};
  sensor::Sensor *last_response_latency_sensor_{nullptr};
  sensor::Sensor *action_queue_depth_sensor_{nullptr};
  sensor::Sensor *action_retry_count_sensor_{nullptr};
  sensor::Sensor *seconds_since_pl1_sensor_{nullptr};
  sensor::Sensor *validation_failures_sensor_{nullptr};
  text_sensor::TextSensor *validation_detail_sensor_{nullptr};

  uint32_t checksum_fail_count_{0};
  uint32_t poll_response_count_{0};
  uint32_t action_retry_count_{0};
  uint32_t last_poll_seen_ms_{0};
  uint32_t validation_failure_count_{0};

  // System Clock publish throttling (Claude Feedback 2026-08-15 change #2).
  uint32_t last_clock_publish_ms_{0};
  bool clock_published_once_{false};

  std::string current_fan_mode_str_{"Normal"};
  std::string current_fan_speed_str_{"Low"};
  bool fan_state_known_{false};

  FanModeSpeed combine_fan_selection_() const;
  static const char *fan_speed_to_str_(uint8_t fan_byte);
  static bool fan_byte_is_continuous_(uint8_t fan_byte);
};

}  // namespace actron_classic
}  // namespace esphome
