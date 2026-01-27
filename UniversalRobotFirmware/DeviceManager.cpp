#include "DeviceManager.h"
#include "ConfigStorage.h"
#include "RS485Manager.h"
#include <FS.h>

#define UNO_COMMAND_THROTTLE_INTERVAL 200 // мс (5 раз в секунду)

DeviceManager::DeviceManager() {}

int DeviceManager::pinToGpio(int pin) {
    if(pin < 0) return -1;
    int gpio = pin;
    switch (pin) {
        case 17: gpio = A0; break; // A0 настроена конкретно здесь на 17, можно будет поменять
        case 0: gpio = 3;  break; // D0 -> GPIO3
        case 1: gpio = 1;  break; // D1 -> GPIO1
        case 2: gpio = 16; break; // D2 -> GPIO16
        case 3:             // D3
        case 15:            // D15 (псевдоним для D3)
            gpio = 5; break;  // -> GPIO5
        case 4:             // D4
        case 14:            // D14 (псевдоним для D4)
            gpio = 4; break;  // -> GPIO4
        case 5:             // D5
        case 13:            // D13 (псевдоним для D5)
            gpio = 14; break; // -> GPIO14
        case 6:             // D6
        case 12:            // D12 (псевдоним для D6)
            gpio = 12; break; // -> GPIO12
        case 7:             // D7
        case 11:            // D11 (псевдоним для D7)
            gpio = 13; break; // -> GPIO13
        case 8: gpio = 0;  break; // D8 -> GPIO0
        case 9: gpio = 2;  break; // D9 -> GPIO2
        case 10: gpio = 15; break; // D10 -> GPIO15
        default: gpio = pin; break; // если придет номер, которого нет в списке
    }
    return gpio;
}

void DeviceManager::setBroadcaster(std::function<void(const String&)> fn) {
  broadcaster = fn;
}

void DeviceManager::setRS485Manager(RS485Manager* manager) {
  rs485Manager = manager;
  if (rs485Manager != nullptr) {
    // Устанавливаем обработчик
    rs485Manager->setOnSensorData([this](byte id, const String& data) {
      this->handleSlaveSensorData(id, data);
    });
  }
}

void DeviceManager::addDevice(const String& name, int pin, int pin2, const String& type) {
  //if (pin < 0) {
  //  Serial.printf("[DeviceManager] addDevice: invalid pin %d\n", pin);
  //  return;
  //}
  if (pin2 >= 0 && pin == pin2) {
    Serial.printf("[DeviceManager] Error: Cannot use the same pin (%d) for both functions of device %s\n", pin, name.c_str());
    return;
  }

  // Автоопределение типа, если тип не задан или пустой
  String actualType = type;
  String lname = name;
  lname.toLowerCase();

  if (actualType == "" || actualType == "unknown") {
    if (lname.indexOf("led") >= 0) actualType = "led";
    else if (lname.indexOf("motor") >= 0) actualType = "motor";
    else if (lname.indexOf("sensor") >= 0 || lname.indexOf("btn") >= 0 || lname.indexOf("switch") >= 0)
      actualType = "sensor";
    else actualType = "generic";
  }

  int gpio = pinToGpio(pin);
  int gpio2 = pinToGpio(pin2);

  // Проверяем, не существует ли уже устройство с таким именем или пином
  for (auto &d : devices) {
    if (d.name == name) {
      d.pin = gpio;
      d.pin2 = gpio2;
      d.type = actualType;
      d.state = false;
      return;
    }
  }

  // Если имя новое, проверяем, не заняты ли пины другими устройствами
  for (auto &d : devices) {
    if (gpio >= 0 && (d.pin == gpio || (d.pin2 >= 0 && d.pin2 == gpio))) {
      Serial.printf("[DeviceManager] Error: Pin %d (GPIO %d) is already used by device '%s'\n", pin, gpio, d.name.c_str());
      return;
    }
    if (gpio2 >= 0 && (d.pin == gpio2 || (d.pin2 >= 0 && d.pin2 == gpio2))) {
      Serial.printf("[DeviceManager] Error: Pin %d (GPIO %d) is already used by device '%s'\n", pin2, gpio2, d.name.c_str());
      return;
    }
  }

  DeviceInfo di;
  di.name = name;
  di.pin = gpio;
  di.pin2 = gpio2;
  di.d_pin = pin;
  di.d_pin2 = pin2;
  di.type = actualType;
  di.state = false;
  di.pwmValue = 0;
  di.subscribed = false;
  devices.push_back(di);

  // Настройка пина по типу
  if (gpio >= 0) {
    if (actualType == "led" || actualType == "motor") {
      pinMode(gpio, OUTPUT);
      digitalWrite(gpio, LOW);
    } else if (actualType == "sensor") {
      pinMode(gpio, INPUT_PULLUP);
    }
  }
  if (gpio2 >= 0) {
    if (actualType == "motor") {
      pinMode(gpio2, OUTPUT);
      digitalWrite(gpio2, LOW);
    }
  }

  Serial.printf("[DeviceManager] Device '%s' added on pin(s) D%d, D%d\n", name.c_str(), pin, pin2);

  StaticJsonDocument<256> doc;
  doc["cmd"] = "device_added";
  doc["name"] = name;
  doc["pin"] = pin;
  doc["type"] = actualType;
  String s; serializeJson(doc, s);
  if (broadcaster) broadcaster(s);
}

void DeviceManager::clearDevices() {
  devices.clear();
}

void DeviceManager::setDeviceState(const String& name, bool on) {
    if (name.startsWith("uno_") && rs485Manager != nullptr) {
        unsigned long now = millis();
        // Это команда для Arduino! Перенаправляем её.
        // Адрес Uno - 1, команда - это имя устройства, значение - 1 (ON) или 0 (OFF).
        if (now - lastUnoCommandMillis[name] > UNO_COMMAND_THROTTLE_INTERVAL) {
          rs485Manager->sendCommand(1, name, on ? 1 : 0);
          lastUnoCommandMillis[name] = now;
        }
        return; // Выходим, чтобы не выполнять локальную логику для ESP
    }
    for (auto &d : devices) {
        if (d.name == name) {
            d.state = on;
            int actualPin = d.pin;
            pinMode(actualPin, OUTPUT);
            
            // Если это встроенный LED (GPIO2)
            if (actualPin == LED_BUILTIN) {
              digitalWrite(actualPin, on ? LOW : HIGH); // инверсное управление
              Serial.printf("[DeviceManager] LED_BUILTIN -> %s (GPIO%d)\n", on ? "ON" : "OFF", actualPin);
            } else {
              digitalWrite(actualPin, on ? HIGH : LOW);
              Serial.printf("[DeviceManager] %s -> %s (GPIO%d)\n", d.name.c_str(), on ? "ON" : "OFF", actualPin);
            }
            notifyDeviceStateChanged(name, on);
            return;
        }
    }
}

void DeviceManager::notifyDeviceStateChanged(const String& name, bool state) {
  if (!broadcaster) return;
  StaticJsonDocument<256> doc;
  doc["cmd"] = "device_state";
  doc["name"] = name;
  doc["state"] = state ? 1 : 0;
  String s; serializeJson(doc, s);
  broadcaster(s);
}

void DeviceManager::loop() {
  unsigned long now = millis();
  // Sensor polling every 200ms
  static unsigned long lastSensorMillis = 0;
  if (now - lastSensorMillis > 200) {
      lastSensorMillis = now;
      for (auto &d : devices) {
          if (d.pin >= 0 && (d.type == "sensor" || d.type == "sensor_analog")) {
              int value = 0;
              if (d.pin == A0) value = analogRead(A0);
              else {
                  pinMode(d.pin, INPUT_PULLUP);
                  value = digitalRead(d.pin);
              }
              // Отправляем обновление состояния
              StaticJsonDocument<256> doc;
              doc["cmd"] = "sensor_update";
              doc["device"] = d.name;
              doc["value"] = value;
              String out;
              serializeJson(doc, out);
              if (broadcaster) broadcaster(out);
          }
      }
  }
    
  if (rs485Manager != nullptr && slave1_has_sensors) {
    // Если мы НЕ ждём ответа
    if (!waitingForSensorResponse) {
      // И пришло время (прошло 200 мс)
      if (now - lastSensorPollMillis > 200) {
        lastSensorPollMillis = now;
        rs485Manager->requestSensorData(1);
        waitingForSensorResponse = true; // Теперь мы ждём ответа
      }
    } else {
      // Если мы ждём ответа, но прошло слишком много времени (Тайм-аут)
      if (now - lastSensorPollMillis > 200) { // Ждём 200 мс
        Serial.println("[RS485] Timeout: No sensor data received from slave.");
        waitingForSensorResponse = false; // Сбрасываем флаг, чтобы попробовать снова
      }
    }
  }
}

String DeviceManager::getDevicesJson() {
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.createNestedArray("devices");
  for (auto &d : devices) {
    JsonObject o = arr.createNestedObject();
    o["name"] = d.name;
    o["pin"] = d.pin;
    o["pin2"] = d.pin2;
    o["type"] = d.type;
    o["state"] = d.state;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String DeviceManager::getStatusJson() {
  StaticJsonDocument<256> doc;
  doc["cmd"] = "status";
  doc["uptime_ms"] = millis();
  doc["device_count"] = (int)devices.size();
  String out;
  serializeJson(doc, out);
  return out;
}

void DeviceManager::saveToConfig(ConfigStorage& storage) {
  StaticJsonDocument<2048> doc;
  JsonArray arr = doc.createNestedArray("devices");
  for (auto &d : devices) {
    JsonObject o = arr.createNestedObject();
    o["name"] = d.name;
    o["type"] = d.type;
    if (d.d_pin != -1) {
      o["pin"] = d.d_pin;
    }
    if (d.d_pin2 != -1) {
      o["pin2"] = d.d_pin2;
    }
  }
  String out;
  serializeJson(doc, out);
  if (storage.write(out)) {
    Serial.println("[Config] Saved config");
  } else {
    Serial.println("[Config] Failed to save config");
  }
  if (rs485Manager != nullptr) {
    bool hasSensors = false; // Локальный флаг
    for (auto &d : devices) {
      if (d.name.startsWith("uno_") && d.type == "sensor") {
        hasSensors = true;
      }
    }
    // Обновляем наш глобальный флаг
    slave1_has_sensors = hasSensors;
    if (hasSensors) {
      Serial.println("[DeviceManager] Slave sensor polling enabled.");
    }
  }
}

void DeviceManager::loadFromConfig(ConfigStorage& storage) {
  String configJson = storage.read();
  if (configJson.length() == 0) {
    Serial.println("[Config] Config file not found or empty. Starting fresh.");
    return;
  }

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, configJson);
  if (err) {
    Serial.print("[Config] Failed to parse config file: ");
    Serial.println(err.c_str());
    return;
  }

  bool hasSensors = false;
  if (doc.containsKey("devices")) {
    clearDevices();
    JsonArray arr = doc["devices"].as<JsonArray>();
    for (JsonObject o : arr) {
      String name = o["name"].as<String>();
      String type = o["type"].as<String>();
      int d_pin1 = o.containsKey("pin") ? o["pin"].as<int>() : -1;
      int d_pin2 = o.containsKey("pin2") ? o["pin2"].as<int>() : -1;
      addDevice(name, d_pin1, d_pin2, type);
      if (name.startsWith("uno_") && type == "sensor") {
        hasSensors = true;
      }
    }
    slave1_has_sensors = hasSensors;
    Serial.printf("[Config] Loaded %d devices. Slave sensor polling: %s\n", devices.size(), hasSensors ? "ON" : "OFF");
  }
}

// ========================= MOTOR CONTROL =========================
void DeviceManager::setDeviceSpeed(const String& name, int speedSigned) {
    if (name.startsWith("uno_") && rs485Manager != nullptr) {
        unsigned long now = millis();
        // Это команда для Arduino! Перенаправляем её.
        // Условно, адрес Arduino Uno будет 1.
        if (now - lastUnoCommandMillis[name] > UNO_COMMAND_THROTTLE_INTERVAL) {
          rs485Manager->sendCommand(1, name, speedSigned); 
          lastUnoCommandMillis[name] = now; // Обновляем время последней отправки
        } 
        return; // Выходим, чтобы не выполнять локальную логику
    }
    speedSigned = constrain(speedSigned, -255, 255);
    
    for (auto &d : devices) {
        if (d.name == name && d.type == "motor") {
            d.pwmValue = speedSigned;
            int pin = d.pin;
            int pin2 = d.pin2;
            if(pin2 < 0) { return; } // проверка дополнительного пина (направление)

            if(speedSigned >= 0) {
              digitalWrite(pin2, HIGH);
            } else {
              digitalWrite(pin2, LOW);
            }
            int pwmValue = map(abs(speedSigned), 0, 255, 0, 1023);
            analogWrite(pin, pwmValue);

            Serial.printf("[Motor] %s: DirPin=%d, PwmPin=%d, Speed=%d\n",
                          d.name.c_str(), pin2, pin, speedSigned);

            StaticJsonDocument<256> doc;
            doc["cmd"] = "speed_changed";
            doc["device"] = d.name;
            doc["value"] = speedSigned;
            String out;
            serializeJson(doc, out);
            if (broadcaster) broadcaster(out);
            return;
        }
    }
    Serial.printf("[DeviceManager] setDeviceSpeed: device %s not found or not motor\n", name.c_str());
}

// ========================= SENSOR SUBSCRIPTION =========================
void DeviceManager::setSubscription(const String& name, bool sub) {
    for (auto &d : devices) {
        if (d.name == name && d.type == "sensor") {
            d.subscribed = sub;
            Serial.printf("[DeviceManager] %s subscription -> %s\n", name.c_str(), sub ? "ON" : "OFF");
            return;
        }
    }
    Serial.printf("[DeviceManager] setSubscription: sensor %s not found\n", name.c_str());
}

// ========================= SENSORS JSON SNAPSHOT =========================
String DeviceManager::getSensorsJson() {
    StaticJsonDocument<1024> doc;
    doc["cmd"] = "sensors";
    JsonArray arr = doc.createNestedArray("list");

    for (auto &d : devices) {
        if (d.type == "sensor") {
            int value = 0;
            if (d.pin == A0) value = analogRead(A0);
            else {
                pinMode(d.pin, INPUT_PULLUP);
                value = digitalRead(d.pin);
            }

            JsonObject o = arr.createNestedObject();
            o["name"] = d.name;
            o["pin"] = d.pin;
            o["value"] = value;
        }
    }

    String out;
    serializeJson(doc, out);
    return out;
}

String DeviceManager::getBoardInfoJson() {
  StaticJsonDocument<256> doc;
  doc["cmd"] = "board_info";

  #if defined(ESP8266)
    doc["board"] = "ESP8266";
    doc["chip_id"] = ESP.getChipId();
    doc["sdk_version"] = ESP.getSdkVersion();
    doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  #elif defined(ESP32)
    doc["board"] = "ESP32";
    doc["chip_id"] = (uint32_t)ESP.getEfuseMac();
    doc["cpu_freq_mhz"] = getCpuFrequencyMhz();
  #elif defined(ARDUINO_AVR_UNO)
    doc["board"] = "Arduino Uno";
  #elif defined(ARDUINO_AVR_NANO)
    doc["board"] = "Arduino Nano";
  #else
    doc["board"] = "Unknown";
  #endif

  String s;
  serializeJson(doc, s);
  return s;
}

void DeviceManager::handleSlaveSensorData(byte slaveId, const String& data) {
  // data = "512,1023,44"
  waitingForSensorResponse = false; // Мы получили ответ, можно сбросить флаг
  // Отправляем эти данные по WebSocket на телефон
  if (!broadcaster) return;

  StaticJsonDocument<256> doc;
  doc["cmd"] = "uno_sensors";
  doc["id"] = slaveId;
  doc["data"] = data; // Отправляем сырую строку, телефон разберется
  
  String out;
  serializeJson(doc, out);
  broadcaster(out);
}
