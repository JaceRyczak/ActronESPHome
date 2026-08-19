#include "protocol.h"

namespace esphome {
namespace actron_classic {

uint16_t crc16_modbus(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < length; pos++) {
    crc ^= static_cast<uint16_t>(data[pos]);
    for (int i = 0; i < 8; i++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;  // low byte first when written to the wire
}

}  // namespace actron_classic
}  // namespace esphome
