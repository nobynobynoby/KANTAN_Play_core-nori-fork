// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include "external_pcf8575.hpp"
#include <M5Unified.h>

namespace kanplay_ns {
//-------------------------------------------------------------------------

bool external_pcf8575_t::init(void)
{
  // PCF8575の存在確認と初期化
  // 全ピンをHIGHに設定（入力モードとして準備）
  bool result = false;
  if (M5.Ex_I2C.start(_i2c_addr, false, _i2c_freq)) {
    // 2バイト書き込み（全ピンHIGH = 0xFFFF）
    result = M5.Ex_I2C.write(0xFF) && M5.Ex_I2C.write(0xFF);
    M5.Ex_I2C.stop();
  }
  
  _exists = result;
  return result;
}

bool external_pcf8575_t::update(uint32_t &button_state)
{
  if (!_exists) return false;
  
  // PCF8575は読み取り前に全ピンをHIGHに書き込む必要がある
  bool success = false;
  
  // 書き込み: 全ピンHIGH
  if (M5.Ex_I2C.start(_i2c_addr, false, _i2c_freq)) {
    if (M5.Ex_I2C.write(0xFF) && M5.Ex_I2C.write(0xFF)) {
      M5.Ex_I2C.stop();
      
      // 読み取り: 16ビット
      uint8_t buf[2];
      if (M5.Ex_I2C.start(_i2c_addr, true, _i2c_freq)) {
        if (M5.Ex_I2C.read(buf, 2, true)) {
          button_state = buf[0] | (buf[1] << 8);
          success = true;
        }
        M5.Ex_I2C.stop();
      }
    } else {
      M5.Ex_I2C.stop();
    }
  }
  
  if (!success) {
    _exists = false;
  }
  
  return success;
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
