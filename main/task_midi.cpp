// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "task_midi.hpp"

#include "common_define.hpp"
#include "system_registry.hpp"
// #include "driver_midi.hpp"

#include "midi/midi_transport_uart.hpp"
#include "midi/midi_transport_ble.hpp"
#include "midi/midi_transport_usb.hpp"

#if __has_include(<freertos/freertos.h>)
 #include <freertos/FreeRTOS.h>
 #include <freertos/task.h>
#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------
class subtask_midi_t {
private:
  midi_driver::MIDIDriver _midi;
  system_registry_t::reg_task_status_t::bitindex_t _task_status_index;

// インスタコード固有のコントロールチェンジが来た時、インスタコードモード連携モードに移行、
// インスタコード連携モードのときはノートコントロール無効にして全ノートを転送
  bool _flg_instachord_link = false;
  bool _flg_instachord_out = false;

public:
  subtask_midi_t(midi_driver::MIDI_Transport* transport, system_registry_t::reg_task_status_t::bitindex_t task_status_index)
  : _midi { transport }
  , _task_status_index { task_status_index }
  {
  }

  void setInstaChordLink(bool enable, bool flg_output = false) {
    _flg_instachord_link = enable;
    _flg_instachord_out = flg_output;
  }

  static void task_func(subtask_midi_t* me)
  {
    registry_t::history_code_t history_code_midi_out = 0;
    uint8_t prev_midi_volume = 0;
    bool prev_tx_enable = false;
    bool prev_rx_enable = false;
    uint8_t prev_slot_key = 255;
    uint8_t channel_volume[def::midi::channel_max];
    uint8_t program_number[def::midi::channel_max];
    memset(channel_volume, 100, sizeof(channel_volume));
    auto midi = &(me->_midi);

#if !defined (M5UNIFIED_PC_BUILD)
    midi->setNotifyTaskHandle(xTaskGetCurrentTaskHandle());
#endif

    for (;;) {

#if defined (M5UNIFIED_PC_BUILD)
      M5.delay(1);
#else
      if (ulTaskNotifyTake(pdTRUE, 0) == 0)
      {
        system_registry.task_status.setSuspend(me->_task_status_index);
        ulTaskNotifyTake(pdTRUE, (prev_rx_enable || prev_tx_enable) ? 1 : 128);
        system_registry.task_status.setWorking(me->_task_status_index);
      } 
#endif
      bool tx_enable = midi->getUseTx();
      bool rx_enable = midi->getUseRx();
      if (rx_enable) {
        if (prev_rx_enable != rx_enable) {
          prev_rx_enable = rx_enable;
        }
        midi->receive();
        midi_driver::MIDI_Message message;
        while (midi->receiveMessage(&message)) {
//  printf("status:%02x  len:%d  data:%02x %02x\n", message.status, message.data.size(), message.data[0], message.data[1]);
          // MIDIスルーフラグ
          bool midi_thru = true;
          uint8_t channel = message.channel;
          uint8_t velocity = 0;
          def::command::command_param_array_t command_param_array;

          if (me->_flg_instachord_link) {
            if (message.type == 0x0B && message.data[0] == 0x0F) { // Control Change ( for InstaChord 連携 )
              if (channel == 0x0E || channel == 0x0F) {
                uint8_t control = message.data[1];
                velocity = (control & 0x40) ? 127 : 0;
                control &= 0x3F; // 0x00 ~ 0x3F
                if (channel == 0x0E) {
                  command_param_array = system_registry.command_mapping_midicc15.getCommandParamArray(control);
                  velocity = 127;
                } else if (channel == 0x0F) {
                  command_param_array = system_registry.command_mapping_midicc16.getCommandParamArray(control);
                }
              }
            }
            if (me->_flg_instachord_out) {
              // 出力が有効な場合は入力側を無効化する
              midi_thru = false;
            }
          }
          if ((message.type & ~1) == 0x08) { // Note On/Off
            velocity = (message.type == 0x09) // NoteOn
                              ? message.data[1]
                              : 0;
            // インスタコード連携モードでない場合は、ノートによる制御のためのコマンドを取得する
            if (!me->_flg_instachord_link) {
              command_param_array = system_registry.command_mapping_midinote.getCommandParamArray(message.data[0]);
              if (!command_param_array.empty() && velocity) {
                system_registry.operator_command.addQueue( { def::command::set_velocity, velocity } );
              }
            }
          }
          if (!command_param_array.empty()) {
            midi_thru = false;
            for (auto command_param : command_param_array.array) {
              uint8_t command = command_param.getCommand();
              if (command == 0) { continue; }
              system_registry.operator_command.addQueue(command_param, velocity ? true : false);
            }
          }
          if (midi_thru == true && message.data.size() <= 2) {
            // MIDIノートがコマンドマッピングされていない場合
            system_registry.midi_out_control.setMessage(message.status, message.data[0], message.data[1]);
          }
          midi->receive();
        }
      }

      bool queued = false;
      if (prev_tx_enable != tx_enable) {
        prev_tx_enable = tx_enable;
        if (tx_enable) {
          prev_midi_volume = 0;
          for (int i = 0; i < 16; ++i) {
            // チャンネルボリュームおよびプログラムチェンジを設定
            uint8_t vol = channel_volume[i];
            midi->sendControlChange(def::midi::channel_1 + i, 7, vol);
            uint8_t prg = program_number[i];
            midi->sendProgramChange(def::midi::channel_1 + i, prg);
          }
          queued = true;
        }
      }
      if (tx_enable) {
        if (me->_flg_instachord_link)
        { // InstaChord連携モードのときは、かんぷれ側のキー変更をインスタコード側に反映する
          int master_key = system_registry.runtime_info.getMasterKey();
          int slot_key = master_key + (int8_t)system_registry.current_slot->slot_info.getKeyOffset();
          while (slot_key < 0) { slot_key += 12; }
          while (slot_key >= 12) { slot_key -= 12; }
          if (prev_slot_key != slot_key) {
            prev_slot_key = slot_key;
            // マスタースロットキー設定
            midi->sendControlChange(def::midi::channel_15, 0x0F, slot_key);
            queued = true;
          }
        }
        if (!me->_flg_instachord_link || me->_flg_instachord_out)
        {
          auto midi_volume = system_registry.user_setting.getMIDIMasterVolume();
          if (prev_midi_volume != midi_volume) {
            prev_midi_volume = midi_volume;
            // マスターボリューム設定
            midi->sendControlChange(def::midi::channel_1, 99, 55);
            midi->sendControlChange(def::midi::channel_1, 98,  7);
            midi->sendControlChange(def::midi::channel_1,  6, midi_volume);
            queued = true;
          }
        }
      }
      const registry_t::history_t* history;
      while (nullptr != (history = system_registry.midi_out_control.getHistory(history_code_midi_out))) {
        uint8_t status = history->index & 0xFF;
        uint8_t midi_ch = status & 0x0F;
        uint8_t data1 = history->value & 0xFF;
        uint8_t data2 = (history->value >> 8) & 0xFF;
        bool send = true;
        switch (status & 0xF0) {
        case 0xB0: // Control Change
          if (data1 == 7) { // Channel Volume
            send = channel_volume[midi_ch] != data2;
            channel_volume[midi_ch] = data2;
          }
          break;

        case 0xC0: // Program Change
          send = program_number[midi_ch] != data1;
          program_number[midi_ch] = data1;
          break;

        default:
          break;
        }
        if (send) {
          if (tx_enable && (!me->_flg_instachord_link || me->_flg_instachord_out)) {
            midi->sendMessage(status, data1, data2);
            queued = true;
          }
        }
      }
      if (queued) {
        // MIDI送信バッファをフラッシュ
        midi->sendFlush();
      }
    }
  }
};

#if defined (M5UNIFIED_PC_BUILD)

static subtask_midi_t* subtask_array[] = {};

#else

static midi_driver::MIDI_Transport_UART in_uart_midi_transport; // かんぷれ内部MIDI
static midi_driver::MIDI_Transport_UART portc_midi_transport; // PortC外部MIDI

static subtask_midi_t in_uart_midi_subtask { &in_uart_midi_transport, system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_INTERNAL };
static subtask_midi_t portc_midi_subtask { &portc_midi_transport, system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_EXTERNAL };

#ifdef MIDI_TRANSPORT_BLE_HPP
static midi_driver::MIDI_Transport_BLE ble_midi_transport; // BLE-MIDI
static subtask_midi_t ble_midi_subtask { &ble_midi_transport, system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_BLE };
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
static midi_driver::MIDI_Transport_USB usb_midi_transport; // USB-MIDI
static subtask_midi_t usb_midi_subtask { &usb_midi_transport, system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_USB };
#endif


static subtask_midi_t* subtask_array[] = {
  &in_uart_midi_subtask, // かんぷれ内部MIDI
  &portc_midi_subtask, // PortC外部MIDI
#ifdef MIDI_TRANSPORT_BLE_HPP
  &ble_midi_subtask, // BLE-MIDI
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
  &usb_midi_subtask, // USB-MIDI
#endif
};
#endif
static constexpr const size_t max_subtask = sizeof(subtask_array)/sizeof(subtask_array[0]);

#if defined (M5UNIFIED_PC_BUILD)
 SDL_Thread* subtask_handle[max_subtask];
#else
 TaskHandle_t subtask_handle[max_subtask];
#endif

void task_midi_t::start(void)
{

#if defined (M5UNIFIED_PC_BUILD)
  // windows_midi_transport_t::config_t config;

  // config.deviceID = 0;
  // windows_midi_transport.init(config);
  // windows_midi_transport.changeEnable(true, false);

  // auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "midi", this);
  for (int i = 0; i < max_subtask; ++i) {
    subtask_handle[i] = SDL_CreateThread((SDL_ThreadFunction)subtask_midi_t::task_func, "midi_subtask", &subtask_array[i]);
  }
#else
  {
    midi_driver::MIDI_Transport_UART::config_t config;

    // 内部SAM音源用MIDI
    config.uart_port_num = 2; // UART_NUM_2;
    config.pin_tx = def::hw::pin::midi_tx;
    config.pin_rx = GPIO_NUM_NC;
    in_uart_midi_transport.setConfig(config);
    in_uart_midi_transport.begin();
    in_uart_midi_transport.setUseTxRx(true, false);

    // 外部PortC用MIDI
    config.uart_port_num = 1; // UART_NUM_1
    config.pin_tx = M5.getPin(m5::pin_name_t::port_c_txd);
    config.pin_rx = M5.getPin(m5::pin_name_t::port_c_rxd);
    portc_midi_transport.setConfig(config);
    portc_midi_transport.begin();
    // オン・オフはsystem_registryで設定する
  }
#ifdef MIDI_TRANSPORT_BLE_HPP
  {
    midi_driver::MIDI_Transport_BLE::config_t config;
    ble_midi_transport.setConfig(config);
    ble_midi_transport.begin();
    // オン・オフはsystem_registryで設定する
  }
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
  {
    midi_driver::MIDI_Transport_USB::config_t config;
    usb_midi_transport.setConfig(config);
    usb_midi_transport.begin();
  }
#endif

  TaskHandle_t handle = nullptr;
  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "midi", 1024*3, this, def::system::task_priority_midi, &handle, def::system::task_cpu_midi);
  system_registry.midi_out_control.setNotifyTaskHandle(handle);
  system_registry.midi_port_setting.setNotifyTaskHandle(handle);

  for (int i = 0; i < max_subtask; ++i) {
    xTaskCreatePinnedToCore((TaskFunction_t)subtask_midi_t::task_func, "midi_subtask", 1024*3, subtask_array[i], def::system::task_priority_midi_sub, &subtask_handle[i], def::system::task_cpu_midi_sub);
  }
#endif
}

void task_midi_t::task_func(task_midi_t* me)
{
#if defined (M5UNIFIED_PC_BUILD)
  for (;;) {
    M5.delay(1);
  }
#else
  bool prev_portc_out = false;
  bool prev_portc_in = false;
#ifdef MIDI_TRANSPORT_BLE_HPP
  bool prev_ble_out = false;
  bool prev_ble_in = false;
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
  bool prev_usb_out = false;
  bool prev_usb_in = false;
#endif

  def::command::instachord_link_port_t prev_iclink_port = def::command::instachord_link_port_t::iclp_off;
  def::command::instachord_link_dev_t prev_iclink_dev = def::command::instachord_link_dev_t::icld_kanplay;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    auto iclink_port = system_registry.midi_port_setting.getInstaChordLinkPort();
    auto iclink_dev = system_registry.midi_port_setting.getInstaChordLinkDev();
    if (prev_iclink_port != iclink_port || prev_iclink_dev != iclink_dev) {
      // 演奏デバイスがInstaChordの場合はアウトプット有効とする
      bool output = (iclink_dev == def::command::instachord_link_dev_t::icld_instachord);
      switch (prev_iclink_port) {
      case def::command::instachord_link_port_t::iclp_ble:
        ble_midi_subtask.setInstaChordLink(false, output);
        break;
      case def::command::instachord_link_port_t::iclp_usb:
        usb_midi_subtask.setInstaChordLink(false, output);
        break;
      default:
        // 何もしない
        break;
      }
      prev_iclink_port = iclink_port;
      prev_iclink_dev = iclink_dev;
      // InstaChord Linkモードのときは、チャンネルボリュームの最大値を 85 にする。
      uint8_t chvol_max = 85;

      switch (iclink_port) {
      case def::command::instachord_link_port_t::iclp_ble:
        ble_midi_subtask.setInstaChordLink(true, output);
        break;
      case def::command::instachord_link_port_t::iclp_usb:
        usb_midi_subtask.setInstaChordLink(true, output);
        break;
      default:
        // InstaChord Linkモードでない場合は、チャンネルボリュームの最大値を127にする
        chvol_max = 127;
        break;
      }
      system_registry.runtime_info.setMIDIChannelVolumeMax(chvol_max);
    }

    auto portc_setting = system_registry.midi_port_setting.getPortCMIDI();
    bool portc_out = portc_setting & def::command::ex_midi_mode_t::midi_output;
    bool portc_in  = portc_setting & def::command::ex_midi_mode_t::midi_input;
    if (prev_portc_out != portc_out || prev_portc_in != portc_in) {
      prev_portc_out = portc_out;
      prev_portc_in  = portc_in;
      portc_midi_transport.setUseTxRx(portc_out, portc_in);
      kanplay_ns::system_registry.runtime_info.setMidiPortStatePC((portc_out || portc_in) ? kanplay_ns::def::command::midiport_info_t::mp_connected : kanplay_ns::def::command::midiport_info_t::mp_off);
    }
#ifdef MIDI_TRANSPORT_BLE_HPP
    auto ble_setting = system_registry.midi_port_setting.getBLEMIDI();
    bool ble_out = ble_setting & def::command::ex_midi_mode_t::midi_output;
    bool ble_in  = ble_setting & def::command::ex_midi_mode_t::midi_input;
    if (iclink_port == def::command::instachord_link_port_t::iclp_ble)
    { // InstaChord Link BLEモードのときは、BLE-MIDIを有効にする
      ble_out = true;
      ble_in = true;
    }
    if (prev_ble_out != ble_out || prev_ble_in != ble_in) {
      bool prev_en = prev_ble_out || prev_ble_in;
      bool en = ble_out || ble_in;
      if (prev_en != en) {
        kanplay_ns::system_registry.runtime_info.setMidiPortStateBLE(en ? kanplay_ns::def::command::midiport_info_t::mp_enabled : kanplay_ns::def::command::midiport_info_t::mp_off);
      }
      prev_ble_out = ble_out;
      prev_ble_in  = ble_in;
      ble_midi_transport.setUseTxRx(ble_out, ble_in);
    }
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
    auto usb_setting = system_registry.midi_port_setting.getUSBMIDI();
    bool usb_out = usb_setting & def::command::ex_midi_mode_t::midi_output;
    bool usb_in  = usb_setting & def::command::ex_midi_mode_t::midi_input;
    if (iclink_port == def::command::instachord_link_port_t::iclp_usb)
    { // InstaChord Link USBモードのときは、USB-MIDIを有効にする
      usb_out = true;
      usb_in = true;
    }
    if (prev_usb_out != usb_out || prev_usb_in != usb_in) {
      bool prev_en = prev_usb_out || prev_usb_in;
      bool en = usb_out || usb_in;
      if (prev_en != en) {
        kanplay_ns::system_registry.runtime_info.setMidiPortStateUSB(en ? kanplay_ns::def::command::midiport_info_t::mp_enabled : kanplay_ns::def::command::midiport_info_t::mp_off);
      }
      prev_usb_out = usb_out;
      prev_usb_in  = usb_in;
      usb_midi_transport.setUseTxRx(usb_out, usb_in);
    }
#endif

    for (int i = 0; i < max_subtask; ++i) {
      xTaskNotifyGive(subtask_handle[i]);
    }
  }
#endif
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
