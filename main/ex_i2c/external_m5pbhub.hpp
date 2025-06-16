#ifndef KANPLAY_EXTERNAL_M5PBHUB_HPP
#define KANPLAY_EXTERNAL_M5PBHUB_HPP

#include "interface_external.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
class external_m5pbhub_t : public interface_external_t {
public:
  constexpr external_m5pbhub_t(uint8_t addr = default_i2c_addr, uint32_t freq = 400000)
  : interface_external_t { addr, freq } {}

  static constexpr const uint8_t default_i2c_addr = 0x61;
  bool init(void) override;
  bool update(uint32_t &button_state) override;
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
