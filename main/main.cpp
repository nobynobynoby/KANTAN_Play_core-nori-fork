// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include <stdio.h>

#include "task_spi.hpp"
#include "task_i2c.hpp"
#include "task_i2s.hpp"
#include "task_midi.hpp"
#include "task_wifi.hpp"
#include "task_port_a.hpp"
#include "task_port_b.hpp"
#include "task_operator.hpp"
#include "task_commander.hpp"
#include "task_kantanplay.hpp"
#include "system_registry.hpp"

static kanplay_ns::task_spi_t task_spi;
static kanplay_ns::task_i2c_t task_i2c;
static kanplay_ns::task_i2s_t task_i2s;
static kanplay_ns::task_midi_t task_midi;
static kanplay_ns::task_wifi_t task_wifi;
static kanplay_ns::task_port_a_t task_port_a;
static kanplay_ns::task_port_b_t task_port_b;
static kanplay_ns::task_operator_t task_operator;
static kanplay_ns::task_commander_t task_commander;
static kanplay_ns::task_kantanplay_t task_kantanplay;

static void log_memory(int index = 0)
{
#if CORE_DEBUG_LEVEL > 3 // && !defined ( M5UNIFIED_PC_BUILD )
  size_t free_heap_size = esp_get_free_heap_size() >> 10;
  size_t malloc_cap_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >> 10;
  size_t malloc_cap_8bit = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >> 10;
  size_t malloc_cap_32bit = heap_caps_get_largest_free_block(MALLOC_CAP_32BIT) >> 10;
  size_t malloc_cap_dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA) >> 10;
  size_t malloc_cap_dma_large = heap_caps_get_largest_free_block(MALLOC_CAP_DMA) >> 10;
  // size_t malloc_cap_spi = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >> 10;
  M5_LOGV("%d Mem heap:%5dKB  Internal:%5dKB  8bit:%5dKB  32bit:%5dKB  DMA free:%5dKB  DMA Large:%5dKB"
  , index, free_heap_size, malloc_cap_internal, malloc_cap_8bit, malloc_cap_32bit, malloc_cap_dma_free, malloc_cap_dma_large);
#endif
}

void setup() {
  log_memory(1); 
  auto cfg = M5.config();
  cfg.output_power = false;
  M5.begin(cfg);

  M5.Display.setRotation(0);
  M5.Display.setTextSize(2);
  M5.Display.printf("KANTAN Play\nver%lu.%lu.%lu\n\nboot"
    , kanplay_ns::def::app::app_version_major, kanplay_ns::def::app::app_version_minor, kanplay_ns::def::app::app_version_patch);

  M5.Power.setExtOutput(true);
  M5.Power.setChargeCurrent(200);

  log_memory(2); M5.delay(16); M5.Display.print("."); kanplay_ns::system_registry.init();
  log_memory(3); M5.delay(16); M5.Display.print("."); task_i2s.start();
  log_memory(4); M5.delay(16); M5.Display.print(".");
  if (!task_i2c.start()) {
    M5.Display.print("\nhardware not found.\n");
    M5.delay(4096);
    M5.Power.powerOff();
  }
  log_memory(5); M5.delay(16); M5.Display.print("."); task_midi.start();
  log_memory(6); M5.delay(16); M5.Display.print("."); task_wifi.start();
  log_memory(7); M5.delay(16); M5.Display.print("."); task_operator.start();
  log_memory(8); M5.delay(16); M5.Display.print("."); task_kantanplay.start();

  kanplay_ns::system_registry.internal_input.setButtonBitmask(0x00);
  kanplay_ns::system_registry.operator_command.addQueue( { kanplay_ns::def::command::slot_select, 1 } );
  kanplay_ns::system_registry.operator_command.addQueue( { kanplay_ns::def::command::file_index_set, 0 } );

  log_memory(9); M5.delay(16); M5.Display.print("."); task_commander.start();
  log_memory(10); M5.delay(16); M5.Display.print("."); task_port_a.start();
  log_memory(11); M5.delay(16); M5.Display.print("."); task_port_b.start();
  log_memory(12); M5.delay(16); M5.Display.print("."); task_spi.start();

  { // 起動直後のファイルを読込
    kanplay_ns::def::app::file_command_info_t songinfo;
    // songinfo.file_index = -1; // 末尾のデータ ( 99_Sample.json ) を読み込むため-1を指定する
    songinfo.file_index = 0; // 先頭のデータ ( Sample.json ) を読み込む
    songinfo.dir_type = kanplay_ns::def::app::data_type_t::data_song_preset;
    kanplay_ns::system_registry.file_command.setFileLoadRequest(songinfo);
  }
}

void loop() {
  M5.delay(1024);
#if !defined ( M5UNIFIED_PC_BUILD )
  log_memory(); 
/*
  {
    auto t = time(nullptr);
    auto tm = localtime(&t);
    static constexpr const char* const wd[7] = {"Sun","Mon","Tue","Wed","Thr","Fri","Sat"};
    M5.Log.printf("localtime:%04d/%02d/%02d (%s)  %02d:%02d:%02d\r\n",
          tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
          wd[tm->tm_wday],
          tm->tm_hour, tm->tm_min, tm->tm_sec);
  }
//*/
#endif
/*
  M5_LOGV("perf: %d %d  spi:%d i2c:%d i2s:%d midi:%d cmd:%d kanplay:%d"
    , kanplay_ns::system_registry.task_status.getLowPowerCounter() >> 10
    , kanplay_ns::system_registry.task_status.getHighPowerCounter() >> 10
    , kanplay_ns::system_registry.task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_SPI_COUNTER) >> 10
    , kanplay_ns::system_registry.task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_I2C_COUNTER) >> 10
    , kanplay_ns::system_registry.task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_I2S_COUNTER) >> 10
    , kanplay_ns::system_registry.task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_MIDI_INTERNAL_COUNTER) >> 10
    , kanplay_ns::system_registry.task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_COMMANDER_COUNTER) >> 10
    , kanplay_ns::system_registry.task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_KANTANPLAY_COUNTER) >> 10
  );
//*/
}

#if !defined (M5UNIFIED_PC_BUILD) && !defined (ARDUINO)
extern "C" void app_main(void)
{
  setup();
  for (;;) {
    loop();
  }
}
#endif
