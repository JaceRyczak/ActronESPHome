#include "actron_zone_switch.h"
#include "../actron_classic.h"
#include "esphome/core/log.h"

namespace esphome {
namespace actron_classic {

static const char *const TAG = "actron_classic.switch";

void ActronZoneSwitch::setup() {
  // Real state populated by ActronClassic once the first valid PL1 arrives.
}

void ActronZoneSwitch::dump_config() { LOG_SWITCH("", "Actron Zone Switch", this); }

void ActronZoneSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) {
    this->parent_->queue_zone_action(this->zone_number_, state);
  }
  // Optimistic UI update; ActronClassic corrects/reverts this once the
  // action has been verified (or given up on) against a future PL1.
  this->publish_state(state);
}

}  // namespace actron_classic
}  // namespace esphome
