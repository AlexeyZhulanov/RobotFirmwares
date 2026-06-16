#pragma once
#include <Arduino.h>

class RS485Manager {
public:
  //RS485Manager(byte receivePin, byte transmitPin);
  RS485Manager();
  void begin();
  void loop();

  // Основная функция: отправить команду ведомому устройству
  void sendCommand(byte slaveId, const String& command, int value);
  void sendStringCommand(byte slaveId, const String& command, const String& payload);
  void requestSensorData(byte slaveId);

  // Устанавливаются DeviceManager'ом, чтобы RS485Manager мог "позвать" его
  void setOnSensorData(std::function<void(byte, const String&)> cb);

private:
  //byte _rxPin, _txPin;

  std::function<void(byte, const String&)> onSensorCallback;
  
  void parseResponse(const String& packet);
};