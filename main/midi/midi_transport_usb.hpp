// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef MIDI_TRANSPORT_USB_HPP
#define MIDI_TRANSPORT_USB_HPP

#include "midi_driver.hpp"

namespace midi_driver {

class MIDI_Transport_USB : public MIDI_Transport {
public:
  struct config_t {
    // int baud_rate = 31250;
    uint16_t buffer_size_tx = 144;
    // uint16_t buffer_size_rx = 144;
    // uint8_t uart_port_num = 0;
    // int8_t pin_tx = -1;
    // int8_t pin_rx = -1;
  };

  MIDI_Transport_USB(void) = default;
  ~MIDI_Transport_USB();

  void setConfig(const config_t& config) { _config = config; }

  bool begin(void) override;
  void end(void) override;
  // size_t write(const uint8_t* data, size_t length) override;
  std::vector<uint8_t> read(void) override;
  void addMessage(const uint8_t* data, size_t length) override;
  bool sendFlush(void) override;

  void setUseTxRx(bool tx_enable, bool rx_enable) override;
  

private:
  static void usb_host_task(MIDI_Transport_USB* me);
  static void usb_client_task(MIDI_Transport_USB* me);

  std::vector<uint8_t> _tx_data;
  config_t _config;
  bool _is_begin = false;
};

} // namespace midi_driver

#endif // MIDI_TRANSPORT_USB_HPP
