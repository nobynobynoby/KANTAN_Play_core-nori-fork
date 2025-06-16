#include "external_m5pbhub.hpp"

#include <M5Unified.h>

namespace kanplay_ns {
//-------------------------------------------------------------------------
static constexpr const uint8_t digitallReadReg[] = {0x44, 0x45, 0x54, 0x55, 0x64, 0x65, 0x74, 0x75};

bool external_m5pbhub_t::init(void)
{
  uint8_t data[8];
  bool result = scanID();
  _exists = result;
  return result;
}

bool external_m5pbhub_t::update(uint32_t &button_state)
{
  if (_exists) {
    uint8_t readbuf[1];
    uint_fast8_t hitcount = 0;
    uint_fast8_t result = 0;
    for (int i = 0; i < 8; ++i) {
      if (M5.Ex_I2C.start(_i2c_addr, false, _i2c_freq)) {
        if (M5.Ex_I2C.write(digitallReadReg[i])
        && M5.Ex_I2C.stop()
        && M5.Ex_I2C.start(_i2c_addr, true, _i2c_freq)
        && M5.Ex_I2C.read(readbuf, 1, true)) {
          ++hitcount;
        }
        M5.Ex_I2C.stop();
        result |= (!(bool)readbuf[0]) << i;
      }
    }
    if (hitcount == 8) {
      button_state |= result;
      return true;
    }
    _exists = false;
  }
  return false;

}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

