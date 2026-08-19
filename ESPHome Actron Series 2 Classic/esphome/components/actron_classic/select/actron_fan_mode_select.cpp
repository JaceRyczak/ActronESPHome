#include "actron_fan_mode_select.h"
#include "../actron_classic.h"
#include "esphome/core/log.h"

namespace esphome {
namespace actron_classic {

static const char *const TAG = "actron_classic.select.fan_mode";

void ActronFanModeSelect::setup() {}

void ActronFanModeSelect::dump_config() { LOG_SELECT("", "Actron Fan Mode Select", this); }

void ActronFanModeSelect::control(const std::string &value) {
  if (this->parent_ != nullptr) {
    this->parent_->on_fan_mode_selected(value);
  }
  this->publish_state(value);
}

}  // namespace actron_classic
}  // namespace esphome
