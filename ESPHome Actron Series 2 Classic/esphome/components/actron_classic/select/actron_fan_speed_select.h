#pragma once

#include "esphome/core/component.h"
#include "esphome/components/select/select.h"

namespace esphome {
namespace actron_classic {

class ActronClassic;

// "Low" / "Med" / "High" -- independent of fan mode, per design document.
class ActronFanSpeedSelect : public select::Select, public Component {
 public:
  void set_parent(ActronClassic *parent) { this->parent_ = parent; }
  void setup() override;
  void dump_config() override;

 protected:
  void control(const std::string &value) override;

  ActronClassic *parent_{nullptr};
};

}  // namespace actron_classic
}  // namespace esphome
