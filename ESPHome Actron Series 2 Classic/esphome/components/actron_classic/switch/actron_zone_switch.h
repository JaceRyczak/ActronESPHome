#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace actron_classic {

class ActronClassic;

class ActronZoneSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ActronClassic *parent) { this->parent_ = parent; }
  void set_zone_number(uint8_t zone_number) { this->zone_number_ = zone_number; }
  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;

  ActronClassic *parent_{nullptr};
  uint8_t zone_number_{1};
};

}  // namespace actron_classic
}  // namespace esphome
