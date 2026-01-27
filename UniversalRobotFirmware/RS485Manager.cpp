#include "RS485Manager.h"
#include <SoftwareSerial.h>

// Создаем "виртуальный" серийный порт для общения по RS-485
SoftwareSerial rs485Serial;

RS485Manager::RS485Manager(byte receivePin, byte transmitPin) {
  _rxPin = receivePin;
  _txPin = transmitPin;
}

void RS485Manager::begin() {
  // Инициализируем наш виртуальный порт
  rs485Serial.begin(9600, SWSERIAL_8N1, _rxPin, _txPin, false);
  Serial.println("[RS485] Manager started on pins RX/TX/DIR: " + String(_rxPin) + "/" + String(_txPin));
}

void RS485Manager::setOnSensorData(std::function<void(byte, const String&)> cb) {
  onSensorCallback = cb;
}

void RS485Manager::requestSensorData(byte slaveId) {
  String packet = String(slaveId) + ":GET_SENSORS:0\n";
  rs485Serial.print(packet);
  rs485Serial.flush();
  delay(10); // Пауза для эха
  Serial.printf("[RS485] Sent: %s", packet.c_str());
}

void RS485Manager::loop() {
  if (rs485Serial.available()) {
    Serial.printf("YES");
    String response = rs485Serial.readStringUntil('\n');
    response.trim();
    if (response.length() > 0) {
      Serial.printf("[RS485] Received response: %s\n", response.c_str());
      parseResponse(response);
    }
  }
}

void RS485Manager::sendCommand(byte slaveId, const String& command, int value) {
  // 1. Формируем пакет данных. Формат: "ID:КОМАНДА:ЗНАЧЕНИЕ\n"
  String packet = String(slaveId) + ":" + command + ":" + String(value) + "\n";
  
  // 2. Отправляем пакет
  rs485Serial.print(packet);
  
  // 3. Ждем, пока все данные будут отправлены
  rs485Serial.flush();
  
  Serial.printf("[RS485] Sent: %s", packet.c_str());
  delay(10);
}

void RS485Manager::parseResponse(const String& packet) {
  // Ожидаем только ответы от сенсоров: "1:SENSORS:512,1023,..."
  
  int id_separator = packet.indexOf(':');
  if (id_separator == -1) return;
  byte id = packet.substring(0, id_separator).toInt();

  int cmd_separator = packet.indexOf(':', id_separator + 1);
  if (cmd_separator == -1) return;
  
  String command = packet.substring(id_separator + 1, cmd_separator);
  String data = packet.substring(cmd_separator + 1);

  // Если это ответ от сенсоров и у нас есть коллбэк, вызываем его
  if (command == "SENSORS" && onSensorCallback) {
    onSensorCallback(id, data);
  }
}
