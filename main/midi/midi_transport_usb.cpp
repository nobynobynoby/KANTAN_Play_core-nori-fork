// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include "midi_transport_usb.hpp"

#include "../common_define.hpp"
#include "../system_registry.hpp"

#if __has_include(<usb/usb_host.h>)
#include <usb/usb_host.h>
#include <string.h>
#include <esp_log.h>
#include <mutex>

namespace midi_driver {

//----------------------------------------------------------------

static MIDI_Transport_USB* _instance = nullptr;
static std::vector<uint8_t> _rx_data;
static std::mutex mutex_rx;

typedef void (*usb_host_enum_cb_t)(const usb_config_desc_t *config_desc);
static usb_host_enum_cb_t _USB_host_enumerate;
static usb_host_client_handle_t Client_Handle;
static usb_device_handle_t Device_Handle;
static bool isMIDI = false;
static bool isMIDIReady = false;
static const size_t MIDI_IN_BUFFERS = 1;
static usb_transfer_t *MIDIOut = NULL;
static usb_transfer_t *MIDIIn[MIDI_IN_BUFFERS] = {NULL};

volatile static bool _tx_request_flip = false;
volatile static bool _tx_process_flip = false;

void _client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
  esp_err_t err;
  switch (event_msg->event)
  {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
      // ESP_LOGI("", "New device address: %d", event_msg->new_dev.address);
      err = usb_host_device_open(Client_Handle, event_msg->new_dev.address, &Device_Handle);
      if (err == ESP_OK) {
        usb_device_info_t dev_info;
        err = usb_host_device_info(Device_Handle, &dev_info);
        if (err == ESP_OK) {
          const usb_device_desc_t *dev_desc;
          err = usb_host_get_device_descriptor(Device_Handle, &dev_desc);
          if (err == ESP_OK) {
            const usb_config_desc_t *config_desc;
            err = usb_host_get_active_config_descriptor(Device_Handle, &config_desc);
            if (err == ESP_OK) {
              (*_USB_host_enumerate)(config_desc);
            }
          }
        }
      }
      break;

    case USB_HOST_CLIENT_EVENT_DEV_GONE:
      kanplay_ns::system_registry.runtime_info.setMidiPortStateUSB(kanplay_ns::def::command::midiport_info_t::mp_enabled);
      break;

    default:
      // ESP_LOGI("", "Unknown value %d", event_msg->event);
      break;
  }
}

void MIDI_Transport_USB::usb_host_task(MIDI_Transport_USB* me)
{
  uint32_t event_flags;
  bool all_clients_gone = false;
  bool all_dev_free = false;
  for (;;) {
    esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (err == ESP_OK) {
      if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
        // ESP_LOGI("", "No more clients");
        isMIDIReady = false;
        kanplay_ns::system_registry.runtime_info.setMidiPortStateUSB(kanplay_ns::def::command::midiport_info_t::mp_enabled);
        // all_clients_gone = true;
      }
      if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
        // ESP_LOGI("", "No more devices");
        // all_dev_free = true;
      }
    }
    else {
      if (err != ESP_ERR_TIMEOUT) {
        // ESP_LOGI("", "usb_host_lib_handle_events: %x flags: %x", err, event_flags);
      }
    }
  }
}

void MIDI_Transport_USB::usb_client_task(MIDI_Transport_USB* me)
{
  for (;;) {
    esp_err_t err = usb_host_client_handle_events(Client_Handle, portMAX_DELAY);
    if ((err != ESP_OK) && (err != ESP_ERR_TIMEOUT)) {
      // ESP_LOGI("", "usb_host_client_handle_events: %x", err);
    }
  }
}

static void midi_transfer_cb(usb_transfer_t *transfer)
{
  // ESP_LOGI("", "midi_transfer_cb context: %d", transfer->context);
  if (Device_Handle == transfer->device_handle) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && USB_EP_DESC_GET_EP_DIR(transfer)) {
      uint8_t *const p = transfer->data_buffer;
      {
        std::lock_guard<std::mutex> lock(mutex_rx);
        for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
          if ((p[i] + p[i+1] + p[i+2] + p[i+3]) == 0) break;
          _rx_data.insert(_rx_data.end(), p + i, p + i + 4);
          ESP_LOGI("", "midi: %02x %02x %02x %02x",
              p[i], p[i+1], p[i+2], p[i+3]);
        }
      }
      esp_err_t err = usb_host_transfer_submit(transfer);
      if (err != ESP_OK) {
        ESP_LOGI("", "usb_host_transfer_submit In fail: %x", err);
      }
      _instance->execTaskNotifyISR();
    }
    else {
      _tx_process_flip = _tx_request_flip;
    }
  }
}

void check_interface_desc_MIDI(const void *p)
{
  const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;

  // USB MIDI
  if ((intf->bInterfaceClass == USB_CLASS_AUDIO)
   && (intf->bInterfaceSubClass == 3) // MIDI_STREAMING
   && (intf->bInterfaceProtocol == 0)
   && (intf->bNumEndpoints != 0))
  {
    isMIDI = true;
    ESP_LOGI("", "Claiming a MIDI device!");
    esp_err_t err = usb_host_interface_claim(Client_Handle, Device_Handle,
        intf->bInterfaceNumber, intf->bAlternateSetting);
    if (err != ESP_OK) ESP_LOGI("", "usb_host_interface_claim failed: %x", err);
  }
}

void prepare_endpoints(const void *p)
{
  const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)p;
  esp_err_t err;

  // must be bulk for MIDI
  if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_BULK) {
    ESP_LOGI("", "Not bulk endpoint: 0x%02x", endpoint->bmAttributes);
    return;
  }
  if (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) {
    for (int i = 0; i < MIDI_IN_BUFFERS; i++) {
      err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &MIDIIn[i]);
      if (err != ESP_OK) {
        MIDIIn[i] = NULL;
        ESP_LOGI("", "usb_host_transfer_alloc In fail: %x", err);
      }
      else {
        MIDIIn[i]->device_handle = Device_Handle;
        MIDIIn[i]->bEndpointAddress = endpoint->bEndpointAddress;
        MIDIIn[i]->callback = midi_transfer_cb;
        MIDIIn[i]->context = (void *)i;
        MIDIIn[i]->num_bytes = endpoint->wMaxPacketSize;
        esp_err_t err = usb_host_transfer_submit(MIDIIn[i]);
        if (err != ESP_OK) {
          ESP_LOGI("", "usb_host_transfer_submit In fail: %x", err);
        }
      }
    }
  }
  else {
    err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &MIDIOut);
    if (err != ESP_OK) {
      MIDIOut = NULL;
      ESP_LOGI("", "usb_host_transfer_alloc Out fail: %x", err);
      return;
    }
    ESP_LOGI("", "Out data_buffer_size: %d", MIDIOut->data_buffer_size);
    MIDIOut->device_handle = Device_Handle;
    MIDIOut->bEndpointAddress = endpoint->bEndpointAddress;
    MIDIOut->callback = midi_transfer_cb;
    MIDIOut->context = NULL;
//    MIDIOut->flags |= USB_TRANSFER_FLAG_ZERO_PACK;
  }
  isMIDIReady = ((MIDIOut != NULL) && (MIDIIn[0] != NULL));
  if (isMIDIReady) {
    kanplay_ns::system_registry.runtime_info.setMidiPortStateUSB(kanplay_ns::def::command::midiport_info_t::mp_connected);
  }
}

void show_config_desc_full(const usb_config_desc_t *config_desc)
{
  const uint8_t *p = &config_desc->val[0];
  uint8_t bLength;
  for (int i = 0; i < config_desc->wTotalLength; i+=bLength, p+=bLength) {
    bLength = *p;
    if ((i + bLength) <= config_desc->wTotalLength) {
      const uint8_t bDescriptorType = *(p + 1);
      switch (bDescriptorType) {
        case USB_B_DESCRIPTOR_TYPE_INTERFACE:
          if (!isMIDI) check_interface_desc_MIDI(p);
          break;
        case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
          if (isMIDI && !isMIDIReady) {
            prepare_endpoints(p);
          }
          break;

        default:
        case USB_B_DESCRIPTOR_TYPE_DEVICE:
        case USB_B_DESCRIPTOR_TYPE_CONFIGURATION:
        case USB_B_DESCRIPTOR_TYPE_STRING:
        case USB_B_DESCRIPTOR_TYPE_DEVICE_QUALIFIER:
        case USB_B_DESCRIPTOR_TYPE_OTHER_SPEED_CONFIGURATION:
        case USB_B_DESCRIPTOR_TYPE_INTERFACE_POWER:
          break;
      }
    }
    else {
      return;
    }
  }
}

MIDI_Transport_USB::~MIDI_Transport_USB()
{
  end();
}

bool MIDI_Transport_USB::begin(void)
{
  return true;
}

void MIDI_Transport_USB::end(void)
{
  if (_is_begin) {
    _is_begin = false;
  }
}

void MIDI_Transport_USB::addMessage(const uint8_t* data, size_t length)
{
  if (MIDIOut == NULL || _use_tx == false) {
    ESP_LOGI("", "MIDIOut is NULL or tx not enabled");
    return;
  }

  // TODO:システムエクスクルーシブの送信には未対応。
  // 必要に応じて実装すること…。

  _tx_data.push_back(data[0] >> 4);
  _tx_data.insert(_tx_data.end(), data, data + 3);
  if (_tx_data.size() >= MIDIOut->data_buffer_size - 4) {
    sendFlush();
  }
}

bool MIDI_Transport_USB::sendFlush(void)
{
  if (_tx_data.empty()) {
    return true;
  }
  if (MIDIOut == NULL || _use_tx == false) {
    ESP_LOGI("", "MIDIOut is NULL or tx not enabled");
    return false;
  }

  // ※ 前の転送が完了する前にMIDIOutを更新してしまうと、送信データが破損する。
  // そのため、転送が完了するまで待機する。
  while (_tx_process_flip != _tx_request_flip) {
    taskYIELD();
  }
  MIDIOut->num_bytes = _tx_data.size();
  memcpy(MIDIOut->data_buffer, _tx_data.data(), _tx_data.size());
  _tx_data.clear();
  _tx_request_flip = !_tx_process_flip;
  auto err = usb_host_transfer_submit(MIDIOut);
  return (err == ESP_OK);
}

std::vector<uint8_t> MIDI_Transport_USB::read(void)
{
  std::vector<uint8_t> rxValueVec;
  std::lock_guard<std::mutex> lock(mutex_rx);
  if (!_rx_data.empty()) {

    static constexpr uint8_t cin_length_table[] = {
       0, 0, 2, 3, 3, 1, 2, 3,
       3, 3, 3, 3, 2, 2, 3, 1,
    };
    for (int i = 0; i < _rx_data.size(); i += 4) {
      uint8_t cin = _rx_data[i] & 0x0f; // Code Index Number
      size_t len = cin_length_table[cin];
      if (len) {
        rxValueVec.insert(rxValueVec.end(), _rx_data.begin() + i + 1, _rx_data.begin() + i + 1 + len);
      }
    }
    _rx_data.clear();
  }
  return rxValueVec;
}

void MIDI_Transport_USB::setUseTxRx(bool tx_enable, bool rx_enable)
{
  if (_use_tx == tx_enable && _use_rx == rx_enable) { return; }

  _instance = this;

  MIDI_Transport::setUseTxRx(tx_enable, rx_enable);
  // M5_LOGD("uart_midi:uart_set_pin: %d", err);

  kanplay_ns::system_registry.runtime_info.setMidiPortStateUSB
  ( (tx_enable || rx_enable)
    ? kanplay_ns::def::command::midiport_info_t::mp_enabled
    : kanplay_ns::def::command::midiport_info_t::mp_off
  );

  if (_is_begin) return;
  _is_begin = true;

  _USB_host_enumerate = show_config_desc_full;

  const usb_host_config_t config = {
    .skip_phy_setup = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  esp_err_t err = usb_host_install(&config);
  // ESP_LOGI("", "usb_host_install: %x", err);

  const usb_host_client_config_t client_config = {
    .is_synchronous = false,
    .max_num_event_msg = 5,
    .async = {
        .client_event_callback = _client_event_callback,
        .callback_arg = Client_Handle
    }
  };
  err = usb_host_client_register(&client_config, &Client_Handle);
  // ESP_LOGI("", "usb_host_client_register: %x", err);

  xTaskCreatePinnedToCore((TaskFunction_t)usb_host_task, "usb_host", 1024*3, this, kanplay_ns::def::system::task_priority_midi_sub, nullptr, kanplay_ns::def::system::task_cpu_midi_sub);
  xTaskCreatePinnedToCore((TaskFunction_t)usb_client_task, "usb_client", 1024*3, this, kanplay_ns::def::system::task_priority_midi_sub + 1, nullptr, kanplay_ns::def::system::task_cpu_midi_sub);
}

//----------------------------------------------------------------

} // namespace midi_driver

#endif
