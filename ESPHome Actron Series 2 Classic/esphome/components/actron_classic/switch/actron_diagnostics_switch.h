#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace actron_classic {

class ActronClassic;

// Purely local (no bus round-trip): toggles whether the diagnostic-category
// entities (bus/action counters, reversing valve, compressor contactor, run
// request, zone status, system clock) publish updates to Home Assistant.
// Their underlying counters/state keep tracking internally regardless -- see
// ActronClassic::set_diagnostics_enabled(). Added per Claude Feedback
// (2026-08-15) change request: "Ability to toggle on/off the diagnostic
// entity publishes into HA using a HA switch entity."
class ActronDiagnosticsSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ActronClassic *parent) { this->parent_ = parent; }
  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;

  ActronClassic *parent_{nullptr};
};

}  // namespace actron_classic
}  // namespace esphome
