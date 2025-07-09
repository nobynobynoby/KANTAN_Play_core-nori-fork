// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef MIDI_TRANSPORT_BLE_HPP
#define MIDI_TRANSPORT_BLE_HPP

#include "midi_driver.hpp"

namespace midi_driver {

class MIDI_Transport_BLE : public MIDI_Transport {
public:
  struct config_t {
    const char* device_name = "KANTAN-Play";
  };

  MIDI_Transport_BLE(void) = default;
  ~MIDI_Transport_BLE();

  void setConfig(const config_t& config) { _config = config; }

  bool begin(void) override;
  void end(void) override;
  // size_t write(const uint8_t* data, size_t length) override;
  // size_t read(uint8_t* data, size_t length) override;

  void addMessage(const uint8_t* data, size_t length) override;
  bool sendFlush(void) override;

  std::vector<uint8_t> read(void) override;

  void setUseTxRx(bool use_tx, bool use_rx) override;

  static void decodeReceive(const uint8_t* data, size_t length);
private:

  std::vector<uint8_t> _tx_data;
  config_t _config;
  uint8_t _tx_runningStatus = 0;
  bool _is_begin = false;
};

} // namespace midi_driver

#endif // MIDI_TRANSPORT_UART_HPP
