// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include "midi_driver.hpp"

namespace midi_driver {

//-------------------------------------------------------------------------

// MIDI ステータスバイトに基づいてデータバイトの長さを取得
// エラー時は -1 を返す
static int getDataByteLength(uint8_t status) {
  if (status < 0x80) { return -1; }
  static constexpr const uint8_t dataByteLengths_0x80_0xE0[] = {
    2, // 0x80 Note Off
    2, // 0x90 Note On
    2, // 0xA0 Polyphonic Key Pressure
    2, // 0xB0 Control Change
    1, // 0xC0 Program Change
    1, // 0xD0 Channel Pressure
    2, // 0xE0 Pitch Bend Change
    0 // 0xF0 System Common Message
  };
  if (status < 0xF0) return dataByteLengths_0x80_0xE0[(status >> 4) - 8];
  static constexpr const int8_t dataByteLengths_0xF0[] = {
    0, // 0xF0 System Exclusive
    1, // 0xF1 System Common
    2, // 0xF2 Song Position Pointer
    1, // 0xF3 Song Select
    0, // 0xF4 Undefined
    0, // 0xF5 Undefined
    0, // 0xF6 Tune Request
    0, // 0xF7 End of System Exclusive
    0, // 0xF8 Timing Clock
    0, // 0xF9 Undefined
    0, // 0xFA Start
    0, // 0xFB Continue
    0, // 0xFC Stop
    0, // 0xFD Undefined
    0, // 0xFE Active Sensing
    0  // 0xFF System Reset
  };
  status &= 0x0F;
  return dataByteLengths_0xF0[status];
}

void MIDIDriver::sendMessage(uint8_t status_byte, uint8_t data1, uint8_t data2)
{
  uint8_t data[3] = { status_byte, data1, data2 };
  size_t dataByteLength = getDataByteLength(status_byte);
  _transport->addMessage(data, dataByteLength + 1);
/*
if (_send_data.size() >= _send_buffer_size - 3) {
  sendFlush();
}
if (_send_runningStatus != status_byte) {
  _send_runningStatus = status_byte;
  sendFlush();
  _send_data.push_back(status_byte);
}
size_t dataByteLength = getDataByteLength(status_byte);
if (dataByteLength > 0) {
  _send_data.push_back(data1);
  if (dataByteLength > 1) {
    _send_data.push_back(data2);
  }
}
*/
}
/*
void MIDI_Encoder::pushMessage(const MIDI_Message& message)
{
  if (_runningStatus != message.status) {
    _runningStatus = message.status;
    _data.push_back(message.status);
  }
  size_t dataByteLength = getDataByteLength(message.status);
  if (dataByteLength > 0) {
    _data.insert(_data.end(), message.data.begin(), message.data.begin() + dataByteLength);
  }
}

void MIDI_Encoder::pushMessage(uint8_t status_byte, uint8_t data1, uint8_t data2)
{
  if (_runningStatus != status_byte) {
    _runningStatus = status_byte;
    _data.push_back(status_byte);
  }
  size_t dataByteLength = getDataByteLength(status_byte);
  if (dataByteLength > 0) {
    _data.push_back(data1);
    if (dataByteLength > 1) {
      _data.push_back(data2);
    }
  }
}
//*/
bool MIDI_Decoder::popMessage(MIDI_Message* message)
{
  if (_data.empty()) { return false; }
// printf("popMessage : data.size:%d\n", _data.size());
  size_t index = 0;
  if (_data[index] & 0x80) { // Status byte
    message->status = _data[index++];
    _runningStatus = message->status;
  } else {
    if (_runningStatus < 0x80) {
      while (index < _data.size() && (_data[index] & 0x80) == 0) { ++index; }
printf("popMessage : invalid data erase : index:%d\n", index);
      _data.erase(_data.begin(), _data.begin() + index);
      // return false;
      _runningStatus = message->status;
    }
    message->status = _runningStatus;
  }
  size_t dataByteLength = getDataByteLength(message->status);
  if (dataByteLength == -1) {
printf("popMessage : invalid data status:%02x\n", message->status);
    _data.erase(_data.begin(), _data.begin() + index);
    return false;
  }

  if (dataByteLength == 0 && message->status == 0xF0)
  { // System Exclusive
    size_t index_end = index;
    while (index_end < _data.size() && _data[index_end] != 0xF7) { ++index_end; }
// printf("sys ex:len:%d  , end:%02x\n", _data.size(), _data[index_end]);
    if (index_end == _data.size()) {
// printf("sys ex:error, data clear\n");
// _data.clear();
      return false;
    }
    message->data.assign(_data.begin() + index, _data.begin() + index_end);
    _data.erase(_data.begin(), _data.begin() + index_end + 1);
    return true;
  }
  if (index + dataByteLength > _data.size()) {
// printf("data len error: index:%d, dataByteLen:%d, data size:%d\n", index, dataByteLength, _data.size());
// _data.clear();
    return false;
  }
  message->data.assign(_data.begin() + index, _data.begin() + index + dataByteLength);
  _data.erase(_data.begin(), _data.begin() + index + dataByteLength);
  return true;
}

//-------------------------------------------------------------------------

} // namespace midi_driver;
