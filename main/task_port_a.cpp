// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "task_port_a.hpp"

#include "common_define.hpp"
#include "system_registry.hpp"

#include "ex_i2c/external_m5bytebutton.hpp"
#include "ex_i2c/external_m5extio2.hpp"
#include "ex_i2c/external_m5pbhub.hpp"
#include "ex_i2c/external_pcf8575.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
// 最大 4台、アドレスは デフォルトを基準に 4単位で上のアドレスを使用(PbHubはアドレスが1つのみ)
static external_m5extio2_t     external_m5extio2[4]     = { { 0x45 }, { 0x49 }, { 0x4D }, { 0x51 } };  // default addr :0x45
static external_m5bytebutton_t external_m5bytebutton[4] = { { 0x47 }, { 0x4B }, { 0x4F }, { 0x53 } };  // default addr :0x47
static external_m5pbhub_t      external_m5pbhub[4]      = { { 0x61 }, { 0x62 }, { 0x63 }, { 0x64 } };  // default addr :0x61

// PCF8575 16ビットI/Oエキスパンダー (2台)
static external_pcf8575_t      external_pcf8575[2]      = { { 0x20 }, { 0x21 } };  // 16-bit devices

// 8ビットデバイスのグループ
static interface_external_t** groups[] =
{ (interface_external_t*[]){ &external_m5extio2[0], &external_m5bytebutton[0], &external_m5pbhub[0], nullptr },
  (interface_external_t*[]){ &external_m5extio2[1], &external_m5bytebutton[1], &external_m5pbhub[1], nullptr },
  (interface_external_t*[]){ &external_m5extio2[2], &external_m5bytebutton[2], &external_m5pbhub[2], nullptr },
  (interface_external_t*[]){ &external_m5extio2[3], &external_m5bytebutton[3], &external_m5pbhub[3], nullptr }
};

// 16ビットデバイスのグループ（各グループが2つの8ビット分=16ビットを担当）
static interface_external_t** groups16[] =
{ (interface_external_t*[]){ &external_pcf8575[0], nullptr },  // グループ0,1を担当
  (interface_external_t*[]){ &external_pcf8575[1], nullptr }   // グループ2,3を担当
};

static constexpr size_t groups_number = sizeof(groups) / sizeof(groups[0]);
static constexpr size_t groups16_number = sizeof(groups16) / sizeof(groups16[0]);

bool task_port_a_t::start(void)
{
  M5.Ex_I2C.begin();
#if defined (M5UNIFIED_PC_BUILD)
#else
  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "port_a", 1024*3, this, def::system::task_priority_port_a, nullptr, def::system::task_cpu_port_a);
#endif
  return true;
}

void task_port_a_t::task_func(task_port_a_t* me)
{
  uint8_t loop_counter = 0xFF;

  for (;;) {
    ++loop_counter;
    uint32_t button_state = 0;
    
    // 8ビットデバイスの処理
    for (size_t group_index = 0; group_index < groups_number; ++group_index) {
      M5.delay(1);
      auto device_array = groups[group_index];

      uint32_t bitmask = 0;
      int j = 0;
      auto device = device_array[0];
      do {
        if (!device->exists()) {
          if (loop_counter == group_index) {
            device->init();
          }  
        } else {
          if (device->exists()) {
            device->update(bitmask);
          }
        }
        device = device_array[++j];
      } while (device != nullptr);
      button_state |= bitmask << (group_index * 8);
    }
    
    // 16ビットデバイスの処理
    for (size_t i = 0; i < groups16_number; ++i) {
      M5.delay(1);
      auto device_array = groups16[i];
      
      uint32_t data16_combined = 0;
      int j = 0;
      auto device = device_array[0];
      do {
        if (!device->exists()) {
          if (loop_counter == (groups_number + i * 2 + j)) {
            device->init();
          }
        } else {
          uint32_t data16 = 0;
          if (device->update(data16)) {
            data16_combined |= data16;  // 複数デバイスの値をOR合成
          }
        }
        device = device_array[++j];
      } while (device != nullptr);
      
      // 16ビットデータを8ビット×2に分割してbutton_stateに統合
      button_state |= (data16_combined & 0xFF) << (i * 16);           // 下位8ビット
      button_state |= ((data16_combined >> 8) & 0xFF) << (i * 16 + 8); // 上位8ビット
    }
    
    // 全グループの状態をレジストリに登録
    for (size_t group_index = 0; group_index < groups_number; ++group_index) {
      system_registry.external_input.setPortABitmask8(group_index, (button_state >> (group_index * 8)) & 0xFF);
    }
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
