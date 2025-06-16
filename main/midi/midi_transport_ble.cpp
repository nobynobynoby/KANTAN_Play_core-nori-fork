// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#if 1

#include "midi_transport_ble.hpp"

#if __has_include(<esp_bt.h>)

#include <BLEDevice.h>
#include <BLE2902.h>
#include <deque>
#include <vector>

#include <esp_bt.h>
#include <esp32-hal-bt.h>

#define MIDI_SERVICE_UUID         "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define MIDI_CHARACTERISTIC_UUID  "7772e5db-3868-4112-a1a9-f2669d106bf3"

namespace midi_driver {

//----------------------------------------------------------------

std::vector<BLEAdvertisedDevice> foundMidiDevices;

static BLEServer *pServer = nullptr;
static BLEService *pService = nullptr;
static BLEAdvertising *pAdvertising = nullptr;
static BLECharacteristic *pCharacteristic = nullptr;
static int _conn_id = -1;
static std::deque<std::vector<uint8_t> > _rx_queue;

static constexpr const size_t _tx_queue_size = 4;
static int _tx_queue_index = 0;
static std::vector<uint8_t> _tx_queue[_tx_queue_size];

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    _conn_id = pServer->getConnId();
    // M5.Lcd.setTextSize(1);
    // M5.Lcd.setCursor(10, 0);
    // M5.Lcd.printf("BLE MIDI Connected. ");
  };
  void onDisconnect(BLEServer* pServer) {
    _conn_id = -1;
    // M5.Lcd.setTextSize(1);
    // M5.Lcd.setCursor(10, 0);
    // M5.Lcd.printf("BLE MIDI Disconnect.");
  }
};

static void decodeReceive(const uint8_t* data, size_t length)
{
  if (length < 3) return; // タイムスタンプ2バイト＋最低1バイト必要

  printf("receive data length : %d  data:", length);
  for (int i = 0; i < length; i++) {
    printf("%02x ", data[i]);
  }
  printf("\n");

  int start = 1; // メッセージ開始位置。最初のバイトはヘッダータイムスタンプとしてスキップ
  int i = start;

  while (i < length) {
    if (data[i] & 0x80) {
      // 0x80以上が来た場合
      if ((i + 1 < length) && (data[i + 1] & 0x80)) {
        // 次のバイトも0x80以上 → 現在の0x80はタイムスタンプ
        if (start < i) {
          std::vector<uint8_t> msg(data + start, data + i);
          if (_rx_queue.size() > 16) _rx_queue.pop_front();
          _rx_queue.push_back(msg);
        }
        // タイムスタンプを除外して次へ
        start = i + 1;
        i++;
        continue;
      } else if ((i + 1 < length) && (data[i + 1] < 0x80)) {
        // 次のバイトが0x80未満 → タイムスタンプはもう一つ手前
        if (start < i - 1) {
          std::vector<uint8_t> msg(data + start, data + i - 1);
          if (_rx_queue.size() > 16) _rx_queue.pop_front();
          _rx_queue.push_back(msg);
        }
        // タイムスタンプは除外済みなので次へ
        start = i;
        i++;
        continue;
      }
    }
    i++;
  }

  // 最後に残った部分も追加
  if (start < length) {
    std::vector<uint8_t> msg(data + start, data + length);
    if (_rx_queue.size() > 16) _rx_queue.pop_front();
    _rx_queue.push_back(msg);
  }
}


class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    decodeReceive(pCharacteristic->getData(), pCharacteristic->getLength());
/*
    auto rxValue = pCharacteristic->getData();
    auto rxValueLen = pCharacteristic->getLength();
// printf("rxValue:%d : %s\n", rxValueLen, (char*)rxValue);
    if (rxValueLen < 2) { return; }

    // uint8_t timestamp_high = rxValue[0];
    int timestamp_low_index = 1;
    std::vector<uint8_t> rxtmp;
    for (int i = 3; i <= rxValueLen; ++i) {
      if (i == rxValueLen || rxValue[i] & 0x80) {
        if (timestamp_low_index + 1 < i) {
          // rxtmpの末尾にrxValue[timestamp_low_index+1]からrxValue[i]までを追加
          rxtmp.insert(rxtmp.end(), rxValue + timestamp_low_index + 1, rxValue + i);
          timestamp_low_index = i;
// printf("rxValueVec:%d : %s\n", rxValueVec.size(), (char*)rxValueVec.data());
          if (_rx_queue.size() > 16) {
            _rx_queue.pop_front();
          }
          _rx_queue.push_back(rxtmp);
        }
      }
    }
*/
  }
};

static bool ble_scan(void)
{
  BLEScan* pBLEScan = BLEDevice::getScan();
  if (pBLEScan == nullptr) {
    return false;
  }

  BLEUUID serviceUUID(MIDI_SERVICE_UUID);

  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->clearResults();
  foundMidiDevices.clear();
  auto foundDevices = pBLEScan->start(1);
  printf("Found %d BLE device(s)\n", foundDevices->getCount());
  for (int i=0; i < foundDevices->getCount(); i++) {
    BLEAdvertisedDevice device = foundDevices->getDevice(i);
    auto deviceStr = "name = \"" + device.getName() + "\", address = "  + device.getAddress().toString();
    if (device.haveServiceUUID() && device.isAdvertisingService(serviceUUID)) {
      printf(" - BLE MIDI device : %s\n", deviceStr.c_str());
      foundMidiDevices.push_back(device);
    }
    else {
      printf(" - Other type of BLE device : %s\n", deviceStr.c_str());
    }
  }
  printf("Total of BLE MIDI devices : %d\n", foundMidiDevices.size());
  return true;
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

size_t MIDI_Transport_BLE::write(const uint8_t* data, size_t length)
{
  if (_tx_enable == false) { return 0; }
  if (_conn_id < 0) { return 0; }

  std::vector<uint8_t> txbuf;
  txbuf.clear();
  txbuf.push_back(0x80); // TODO:BLEMIDI TimeStamp High
  txbuf.push_back(0x80); // TODO:BLEMIDI TimeStamp Low
  txbuf.insert(txbuf.end(), data, data + length);
  pCharacteristic->setValue( txbuf.data(), txbuf.size());
  // pCharacteristic->setValue( const_cast<uint8_t*>(data), length);
  pCharacteristic->notify();
/*
  std::vector<uint8_t> txValueVec(data, data + length);
  if (_tx_queue.size() > 16) {
    _tx_queue.pop_front();
  }
  _tx_queue.push_back(txValueVec);
*/
  return length;
}

size_t MIDI_Transport_BLE::read(uint8_t* data, size_t length)
{
  if (_rx_enable == false) { return 0; }
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

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  decodeReceive(pData, length);
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
    printf("ble client: onConnect\n");
  }

  void onDisconnect(BLEClient* pclient) {
    // connected = false;
    printf("ble client: onDisconnect\n");
  }
};

void MIDI_Transport_BLE::setEnable(bool tx_enable, bool rx_enable)
{
  if (_tx_enable == tx_enable && _rx_enable == rx_enable) { return; }

  auto midi_service_uuid = BLEUUID(MIDI_SERVICE_UUID);
  auto midi_characteristic_uuid = BLEUUID(MIDI_CHARACTERISTIC_UUID);

  bool prev_en = _tx_enable || _rx_enable;
  bool new_en = tx_enable || rx_enable;
  if (prev_en != new_en) {
    _rx_queue.clear();
    if (new_en) {
      if (!_is_begin) {
        _is_begin = true;
        BLEDevice::init(_config.device_name);
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());

        pService = pServer->createService(midi_service_uuid);
        pCharacteristic = pService->createCharacteristic(
                            midi_characteristic_uuid,
                            BLECharacteristic::PROPERTY_READ   |
                            BLECharacteristic::PROPERTY_WRITE  |
                            BLECharacteristic::PROPERTY_NOTIFY |
                            BLECharacteristic::PROPERTY_WRITE_NR
                          );
        pCharacteristic->setCallbacks(new MyCallbacks());
        pCharacteristic->addDescriptor(new BLE2902());

        BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
        oAdvertisementData.setFlags(ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
        oAdvertisementData.setCompleteServices(midi_service_uuid);
        oAdvertisementData.setName(_config.device_name);
        pAdvertising = pServer->getAdvertising();
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMaxPreferred(0x12);
        pAdvertising->setAdvertisementData(oAdvertisementData);
      }
      pService->start();
      pAdvertising->start();
    } else {
      pAdvertising->stop();
      pService->stop();
    }
  }
  if (pCharacteristic != nullptr) {
    if (tx_enable) {
      pCharacteristic->setNotifyProperty(true);
    } else {
      pCharacteristic->setNotifyProperty(false);
    }
  }
  _tx_enable = tx_enable;
  _rx_enable = rx_enable;

  if (new_en) {
    ble_scan();

    if (!foundMidiDevices.empty()) {
      BLEAddress addr = foundMidiDevices[0].getAddress();
      BLEClient* pClient = BLEDevice::createClient();

      pClient->setClientCallbacks(new MyClientCallback());

printf("try connect :%s\n", foundMidiDevices[0].getName().c_str());
      pClient->connect(&foundMidiDevices[0]);     
      auto remoteservice = pClient->getService(midi_service_uuid);
printf("remoteservice:%p\n", remoteservice);
      auto remotecharacteristic = remoteservice->getCharacteristic(midi_characteristic_uuid);
printf("remotecharacteristic:%p\n", remotecharacteristic);

if (remotecharacteristic->canRead()) {  printf("canRead\n"); }
if (remotecharacteristic->canWrite()) {  printf("canWrite\n"); }
if (remotecharacteristic->canNotify()) {  printf("canNotify\n"); }
if (remotecharacteristic->canIndicate()) { printf("canIndicate\n"); }

      // 再接続(再接続必要デバイスのため)
      pClient->disconnect();
      pClient->connect(&foundMidiDevices[0]);
      
      remotecharacteristic->registerForNotify(notifyCallback);

    }
  }
}

//----------------------------------------------------------------

} // namespace midi_driver

#endif

#endif
