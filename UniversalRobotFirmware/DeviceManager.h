#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include <functional>

struct DeviceInfo {
  String name;        // Имя устройства
  int pin;            // GPIO-пин
  int pin2;           // Второй GPIO-пин (необязательный)
  int d_pin;          // Оригинальный D-пин (как его видит пользователь)
  int d_pin2;         // Оригинальный D-пин для второго пина
  String type;        // Тип устройства ("led", "motor", "sensor", "input" и т.п.)
  bool state;         // Включено/выключено
  int pwmValue;       // Для управления скоростью (0–255)
  bool subscribed;    // Подписан ли клиент на обновления (датчики и т.п.)
};

class ConfigStorage; // forward

class RS485Manager; // Forward declaration

class DeviceManager {
public:
  DeviceManager();
  void setRS485Manager(RS485Manager* manager);
  void handleSlaveSensorData(byte slaveId, const String& data);

  // lifecycle
  void begin(); // mount FS called already in main, this loads config
  void loop();

  // configuration
  void addDevice(const String& name, int pin, int pin2, const String& type);
  void clearDevices();
  void loadFromConfig(ConfigStorage& storage);
  void saveToConfig(ConfigStorage& storage);

  // actions
  void setDeviceState(const String& name, bool on);
  void setDeviceSpeed(const String& name, int speed8bit);
  void setSubscription(const String& name, bool sub);

  // queries
  String getDevicesJson();
  String getStatusJson();
  String getDetectedPinsJson();
  String getSensorsJson();
  String getBoardInfoJson();

  // broadcaster: set by WebServerManager to allow DeviceManager push events
  void setBroadcaster(std::function<void(const String&)> fn);

private:
  RS485Manager* rs485Manager = nullptr;
  bool slave1_has_sensors = false;
  unsigned long lastSensorPollMillis = 0;
  std::vector<DeviceInfo> devices;
  unsigned long lastSensorMillis = 0;
  String lastPinsJson;
  bool dirtyPins;
  bool waitingForSensorResponse = false;

  std::function<void(const String&)> broadcaster;

  std::map<String, unsigned long> lastUnoCommandMillis;

  int pinToGpio(int pin);
  String detectPinsInternal();
  void notifyPinsChanged();
  void notifyDeviceStateChanged(const String& name, bool state);
};
