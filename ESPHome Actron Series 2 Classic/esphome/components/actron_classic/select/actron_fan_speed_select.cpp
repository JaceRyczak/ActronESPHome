#include "actron_fan_speed_select.h"
#include "../actron_classic.h"
#include "esphome/core/log.h"

namespace esphome {
namespace actron_classic {

static const char *const TAG = "actron_classic.select.fan_speed";

void ActronFanSpeedSelect::setup() {}

void ActronFanSpeedSelect::dump_config() { LOG_SELECT("", "Actron Fan Speed Select", this); }

void ActronFanSpeedSelect::control(const std::string &value) {
  if (this->parent_ != nullptr) {
    this->parent_->on_fan_speed_selected(value);
  }
  this->publish_state(value);
}

}  // namespace actron_classic
}  // namespace esphome
