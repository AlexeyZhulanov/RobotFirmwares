#pragma once
#include <Arduino.h>
#include <WebSocketsServer.h>
#include "DeviceManager.h"
#include "ConfigStorage.h"

class WebServerManager {
public:
  WebServerManager(int port, DeviceManager& dm, ConfigStorage& storage);
  void begin();
  void loop();
  std::function<void()> onClientConnected;
  std::function<void()> onClientDisconnected;

private:
  WebSocketsServer ws;
  DeviceManager& deviceManager;
  ConfigStorage& storage;

  int pendingClient;
  unsigned long pendingClientTimestamp;
  unsigned long lastConnectMillis;
  bool busyBroadcast;
  
  void handleMessage(uint8_t num, String msg);
  void broadcast(const String& s);
};