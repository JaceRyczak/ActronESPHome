#include "actron_diagnostics_switch.h"
#include "../actron_classic.h"
#include "esphome/core/log.h"

namespace esphome {
namespace actron_classic {

static const char *const TAG = "actron_classic.switch";

void ActronDiagnosticsSwitch::setup() {
  // ESPHome's Switch base class applies restore_mode (if configured in YAML)
  // before write_state() is first invoked, so the hub picks up the restored
  // on/off value automatically -- no extra restore logic needed here. Set
  // restore_mode: RESTORE_DEFAULT_ON in YAML to keep today's always-on
  // diagnostics behaviour across reboots.
}

void ActronDiagnosticsSwitch::dump_config() { LOG_SWITCH("", "Actron Diagnostics Enabled", this); }

void ActronDiagnosticsSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) {
    this->parent_->set_diagnostics_enabled(state);
  }
  this->publish_state(state);
}

}  // namespace actron_classic
}  // namespace esphome
