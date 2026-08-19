#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace actron_classic {

class ActronClassic;  // full definition not needed here, only a pointer

// Represents System Mode + Temperature Setpoint + Temperature Actual (System
// Power on/off is folded in via CLIMATE_MODE_OFF). Fan mode/speed are
// deliberately NOT exposed through this climate entity -- the design
// document asks for Fan Mode and Fan Speed as two independent, separately
// selectable entities (see select/), which a single-dimension ESPHome
// climate fan_mode trait cannot represent cleanly.
class ActronClimate : public climate::Climate, public Component {
 public:
  void set_parent(ActronClassic *parent) { this->parent_ = parent; }
  void setup() override;
  void dump_config() override;

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  ActronClassic *parent_{nullptr};
};

}  // namespace actron_classic
}  // namespace esphome
