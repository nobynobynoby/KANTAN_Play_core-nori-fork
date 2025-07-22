// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#if 1

#include "midi_transport_ble.hpp"

#if __has_include(<esp_bt.h>)

#include "../system_registry.hpp"

#include <M5Unified.h>

#include <BLEDevice.h>
#include <BLE2902.h>
#include <deque>
#include <vector>

#include <esp_bt.h>
#include <esp32-hal-bt.h>
#include <mutex>

#define MIDI_SERVICE_UUID         "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define MIDI_CHARACTERISTIC_UUID  "7772e5db-3868-4112-a1a9-f2669d106bf3"

namespace midi_driver {

static std::mutex mutex_rx;

//----------------------------------------------------------------

static MIDI_Transport_BLE* _instance = nullptr;

// peripheral BLE client
static BLEClient* _pClient = nullptr;


static BLEServer *pServer = nullptr;
static BLEService *pService = nullptr;
static BLEAdvertising *pAdvertising = nullptr;
static BLECharacteristic *pCharacteristic = nullptr;
static int _conn_id = -1;
// static std::deque<std::vector<uint8_t> > _rx_queue;
static std::vector<uint8_t> _rx_data;

// InstaChordと直結時のCharacteristic
static BLERemoteCharacteristic* remotecharacteristic = nullptr;
static uint16_t _mtu_size = 23;

// static constexpr const size_t _tx_queue_size = 4;
// static int _tx_queue_index = 0;
// static std::vector<uint8_t> _tx_queue[_tx_queue_size];

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
    _conn_id = pServer->getConnId();
// printf("BLE MIDI Connected.\n");
// pServer->updatePeerMTU(_conn_id, _mtu_size);
    // M5.Lcd.printf("BLE MIDI Connected. MTU:%d\n", _mtu_size);
  };
  void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
    _conn_id = -1;
// printf("BLE MIDI Disconnect.\n");
  }
  void onMtuChanged(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
// printf("BLE onMtuChanged : %d\n", param->mtu.mtu);
  }
};
static MyServerCallbacks myServerCallbacks;

void MIDI_Transport_BLE::decodeReceive(const uint8_t* data, size_t length)
{
  if (length < 2) { return; }
  /*
  ESP_LOGV("BLE", "receive data length : %d  data:", length);
  printf("ble receive len:%d  data:", length);
  for (int i = 0; i < length; i++) {
    printf(" %02x", data[i]);
    // ESP_LOGV("BLE", "%02x ", data[i]);
  }
  printf("\n");
  fflush(stdout);
//*/
  // uint8_t timestamp_high = data[0];
  size_t timestamp_low_index = 0;
  if (data[1] & 0x80) {
    timestamp_low_index = 1;
  }
  std::vector<uint8_t> rxtmp;
  for (size_t i = timestamp_low_index + 1; i <= length; ++i) {
    if (i == length || data[i] & 0x80) {
      if (timestamp_low_index + 1 < i) {
        // rxtmpの末尾にdata[timestamp_low_index+1]からrxValue[i]までを追加
        rxtmp.insert(rxtmp.end(), data + timestamp_low_index + 1, data + i);
//   printf("split:%0d-%0d\n", timestamp_low_index + 1, i);
        timestamp_low_index = i;
      }
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_rx);
    _rx_data.insert(_rx_data.end(), rxtmp.begin(), rxtmp.end());
  }
}

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param) override {
    _instance->decodeReceive(pCharacteristic->getData(), pCharacteristic->getLength());
    _instance->execTaskNotifyISR();
    printf("onWrite called.\n");
    fflush(stdout);
  }
  // void onRead(BLECharacteristic *pCharacteristic) override {
  //   printf("onRead called.\n");
  // }
  // void onNotify(BLECharacteristic *pCharacteristic) override {
  //   printf("onNotify called.\n");
  // }

};

static std::vector<BLEAdvertisedDevice> ble_scan(void)
{
  std::vector<BLEAdvertisedDevice> foundMidiDevices;
  BLEScan* pBLEScan = BLEDevice::getScan();
  if (pBLEScan == nullptr) {
    return foundMidiDevices;
  }

  BLEUUID serviceUUID(MIDI_SERVICE_UUID);

  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->clearResults();
  auto foundDevices = pBLEScan->start(1);
  ESP_LOGV("BLE", "Found %d BLE device(s)", foundDevices->getCount());
  for (int i=0; i < foundDevices->getCount(); i++) {
    BLEAdvertisedDevice device = foundDevices->getDevice(i);
    auto deviceStr = "name = \"" + device.getName() + "\", address = "  + device.getAddress().toString();
    if (device.haveServiceUUID() && device.isAdvertisingService(serviceUUID)) {
      ESP_LOGV("BLE", " - BLE MIDI device : %s", deviceStr.c_str());
      foundMidiDevices.push_back(device);
    }
    else {
      ESP_LOGV("BLE", " - Other type of BLE device : %s", deviceStr.c_str());
    }
  }
  ESP_LOGV("BLE", "Total of BLE MIDI devices : %d", foundMidiDevices.size());
  pBLEScan->clearResults();
  return foundMidiDevices;
;
/*
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->clearResults();
  foundMidiDevices.clear();
  BLEScanResults foundDevices = pBLEScan->start(3);
  pBLEScan->clearResults();
  pBLEScan->setAdvertisedDeviceCallbacks(&adv_cb);
  pBLEScan->start(60, true);
*/
}



MIDI_Transport_BLE::~MIDI_Transport_BLE()
{
  end();
}

bool MIDI_Transport_BLE::begin(void)
{
  _is_begin = false;
  return true;
}

void MIDI_Transport_BLE::end(void)
{
  if (_is_begin) {
    _is_begin = false;
    if (_conn_id >= 0) {
      pServer->disconnect(_conn_id);
      _conn_id = -1;
    }
  }
}

void MIDI_Transport_BLE::addMessage(const uint8_t* data, size_t length)
{
  uint32_t msec = M5.millis();
  if (_tx_data.size() + length >= _mtu_size - 3) {
    // If the tx_data size exceeds the buffer size, send it immediately
    sendFlush();
  }
  if (_tx_data.empty()) {
    uint32_t msec_high = 0x3F & (msec >> 7);
    _tx_data.push_back(0x80 | msec_high);
    _tx_runningStatus = 0;
  }

  if (_tx_runningStatus != data[0])
  {
    uint32_t msec_low = (msec & 0x7F);
    _tx_data.push_back(0x80 | msec_low);
    _tx_data.push_back(data[0]); // status byte
    _tx_runningStatus = data[0];
  }
  _tx_data.insert(_tx_data.end(), data + 1, data + length);
  if (_tx_data.size() + 4 >= _mtu_size - 3) {
    // If the tx_data size exceeds the buffer size, send it immediately
    sendFlush();
  }
}

bool MIDI_Transport_BLE::sendFlush(void)
{
  bool result = false;
ESP_LOGV("BLE", "sendFlush called, tx_data size: %d", _tx_data.size());
// printf("sendFlush called, tx_data size: %d\n", _tx_data.size());
  auto remote = remotecharacteristic;
  if (remote)
  {
    if (!_tx_data.empty()) {
      remote->writeValue(_tx_data.data(), _tx_data.size(), false);
    }
    result = true;
  } else if (pCharacteristic) {
    if (!_tx_data.empty()) {
      pCharacteristic->setValue( _tx_data.data(), _tx_data.size());
      pCharacteristic->notify();
    }
    result = true;
  }
  _tx_data.clear();
  _tx_runningStatus = 0;
  return result;
}

std::vector<uint8_t> MIDI_Transport_BLE::read(void)
{
  std::lock_guard<std::mutex> lock(mutex_rx);
  auto rxValueVec = _rx_data;
  _rx_data.clear();
  return rxValueVec;
}
/*
size_t MIDI_Transport_BLE::read(uint8_t* data, size_t length)
{
  if (_use_rx == false) { return 0; }
  if (_conn_id < 0) { return 0; }
  size_t result = 0;

  while (!_rx_queue.empty())
  {
    std::vector<uint8_t> rxValueVec = _rx_queue.front();
    _rx_queue.pop_front();
    size_t copy_length = std::min(length, rxValueVec.size());
    std::copy(rxValueVec.begin(), rxValueVec.begin() + copy_length, data);
    result += copy_length;
    length -= copy_length;
    data += copy_length;
    if (length == 0) { break; }
  }
  return result;
}
//*/

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  _instance->decodeReceive(pData, length);
  _instance->execTaskNotifyISR();
  // printf("Notify callback for characteristic ");
  // printf(pBLERemoteCharacteristic->getUUID().toString().c_str());
  // printf(" of data length : %d  data:", length);
  // for (int i = 0; i < length; i++) {
  //   printf("%02x ", pData[i]);
  // }
  // printf("\n");
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    ESP_LOGV("BLE", "ble client: onConnect");
    // printf("ble client: onConnect\n");
  }

  void onDisconnect(BLEClient* pclient) {
    // connected = false;
    kanplay_ns::system_registry.runtime_info.setMidiPortStateBLE(kanplay_ns::def::command::midiport_info_t::mp_enabled);
    ESP_LOGV("BLE", "ble client: onDisconnect\n");
    printf("ble client: onDisconnect\n");
    if (remotecharacteristic != nullptr) {
      remotecharacteristic = nullptr;
    }
    if (pclient == _pClient) {
      _pClient = nullptr;
      delete pclient;
    }
  }
};
static MyClientCallback myClientCallback;

void MIDI_Transport_BLE::setUseTxRx(bool use_tx, bool use_rx)
{
  _instance = this;
  if (_use_tx == use_tx && _use_rx == use_rx) { return; }

  auto midi_service_uuid = BLEUUID(MIDI_SERVICE_UUID);
  auto midi_characteristic_uuid = BLEUUID(MIDI_CHARACTERISTIC_UUID);

  bool prev_en = _use_tx || _use_rx;
  bool new_en = use_tx || use_rx;
  if (prev_en != new_en) {
    _rx_data.clear();
    if (new_en) {
      kanplay_ns::system_registry.runtime_info.setMidiPortStateBLE(kanplay_ns::def::command::midiport_info_t::mp_enabled);

      if (!_is_begin) {
        _is_begin = true;
        // BLEDevice::setMTU(_mtu_size);
        BLEDevice::init(_config.device_name);
        // BLEDevice::setMTU(_mtu_size);
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(&myServerCallbacks);

        pService = pServer->createService(midi_service_uuid);
        pCharacteristic = pService->createCharacteristic(
                            midi_characteristic_uuid,
                            BLECharacteristic::PROPERTY_READ   |
                            BLECharacteristic::PROPERTY_WRITE_NR|
                            BLECharacteristic::PROPERTY_NOTIFY
                          );
        if (pCharacteristic != nullptr) {
          pCharacteristic->setCallbacks(new MyCallbacks());
          pCharacteristic->addDescriptor(new BLE2902());
  
          BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
          oAdvertisementData.setFlags(ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
          oAdvertisementData.setCompleteServices(midi_service_uuid);
          oAdvertisementData.setName(_config.device_name);
          pAdvertising = pServer->getAdvertising();
          pAdvertising->setMinPreferred(0x06); // 7.5msec  (6 x 1.25msec)
          pAdvertising->setMaxPreferred(0x12);
          pAdvertising->setAdvertisementData(oAdvertisementData);
        }
      }
      pService->start();
      pAdvertising->start();
    } else {
      pAdvertising->stop();
      pService->stop();
    }
  }
  if (pCharacteristic != nullptr) {
    if (use_tx) {
      pCharacteristic->setNotifyProperty(true);
    } else {
      pCharacteristic->setNotifyProperty(false);
    }
  }
  if (new_en) {
    M5.delay(128); // Wait for advertising to start
// BLEDevice::setMTU(_mtu_size);
    auto foundMidiDevices = ble_scan();

    if (!foundMidiDevices.empty()) {
      // BLEAddress addr = foundMidiDevices[0].getAddress();
      if (_pClient != nullptr) {
        _pClient->disconnect();
        delete _pClient;
        _pClient = nullptr;
      }
      BLEClient* pClient = BLEDevice::createClient();

      pClient->setClientCallbacks(&myClientCallback);
/*
while (!pClient->setMTU(mtu)) {
  mtu = mtu / 2;
}
*/
// ESP_LOGV("BLE", "try connect :%s", foundMidiDevices[0].getName().c_str());
// pClient->setMTU(_mtu_size);
      pClient->connect(&foundMidiDevices[0]);
      do {
        // printf(".");
        // fflush(stdout);
        M5.delay(16);
      } while (!pClient->isConnected());
      pClient->disconnect();
      M5.delay(16);
      pClient->connect(&foundMidiDevices[0]);
      do {
        // printf(".");
        // fflush(stdout);
        M5.delay(16);
      } while (!pClient->isConnected());
      _pClient = pClient;

      // printf("\n");
      // fflush(stdout);
      // bool result = pClient->setMTU(_mtu_size);
// printf("BLE MTU set to %d, result: %s\n", _mtu_size, result ? "success" : "failed");
      auto remoteservice = pClient->getService(midi_service_uuid);
// ESP_LOGV("BLE", "remoteservice:%p", remoteservice);
      if (remoteservice != nullptr) {
        // remoteservice->getClient()->setMTU(_mtu_size);
        remotecharacteristic = remoteservice->getCharacteristic(midi_characteristic_uuid);
/*
if (remotecharacteristic->canRead()            ) { printf("canRead\n"); }
if (remotecharacteristic->canWrite()           ) { printf("canWrite\n"); }
if (remotecharacteristic->canWriteNoResponse() ) { printf("canWriteNoResponse\n"); }
if (remotecharacteristic->canNotify()          ) { printf("canNotify\n"); }
if (remotecharacteristic->canIndicate()        ) { printf("canIndicate\n"); }
fflush(stdout);
//*/
        if (remotecharacteristic != nullptr) {
          remotecharacteristic->registerForNotify(notifyCallback);
          kanplay_ns::system_registry.runtime_info.setMidiPortStateBLE(kanplay_ns::def::command::midiport_info_t::mp_connected);
        }
      } else {
        ESP_LOGE("BLE", "Failed to find MIDI service on remote device");
        pClient->disconnect();
      }
    }
  }
  _use_tx = use_tx;
  _use_rx = use_rx;
}

//----------------------------------------------------------------

} // namespace midi_driver

#endif

#endif
