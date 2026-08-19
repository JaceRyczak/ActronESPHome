#include "actron_climate.h"
#include "../actron_classic.h"
#include "esphome/core/log.h"

namespace esphome {
namespace actron_classic {

static const char *const TAG = "actron_classic.climate";

void ActronClimate::setup() {
  // Real state is pushed in from ActronClassic once the first valid PL1 is
  // decoded (see ActronClassic::decode_and_publish_pl1_). Until then this
  // entity reports "unknown", matching the design document's startup
  // behaviour requirement.
}

void ActronClimate::dump_config() { LOG_CLIMATE("", "Actron Classic Climate", this); }

climate::ClimateTraits ActronClimate::traits() {
  auto traits = climate::ClimateTraits();
  // Note: ESPHome 2026.5.0 removed the boolean set_supports_*() accessors in
  // favour of feature flags. We only need a single (not two-point) target
  // temperature, so that flag is simply left unset (default off).
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_FAN_ONLY,
      climate::CLIMATE_MODE_HEAT_COOL,  // Actron "Auto"
  });
  traits.set_visual_min_temperature(SETPOINT_MIN_C);
  traits.set_visual_max_temperature(SETPOINT_MAX_C);
  traits.set_visual_temperature_step(SETPOINT_STEP_C);
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_ACTION);
  return traits;
}

void ActronClimate::control(const climate::ClimateCall &call) {
  if (this->parent_ == nullptr) return;

  if (call.get_mode().has_value()) {
    this->parent_->request_hvac_mode(*call.get_mode());
    this->mode = *call.get_mode();
  }
  if (call.get_target_temperature().has_value()) {
    float target = *call.get_target_temperature();
    this->parent_->queue_temp_action(target);
    this->target_temperature = target;
  }
  // Optimistic UI update -- ActronClassic will re-publish the confirmed (or
  // reverted) value once the action queue resolves against a future PL1.
  this->publish_state();
}

}  // namespace actron_classic
}  // namespace esphome
