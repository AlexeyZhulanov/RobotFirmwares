#include "WebServerManager.h"
#include <ArduinoJson.h>

WebServerManager::WebServerManager(int port, DeviceManager& dm, ConfigStorage& storageRef)
  : ws(port), deviceManager(dm), storage(storageRef) {
  pendingClient = -1;
  lastConnectMillis = 0;
  busyBroadcast = false;
}

void WebServerManager::begin() {
  // set broadcaster in deviceManager so it can push messages
  deviceManager.setBroadcaster([this](const String& s){
    this->broadcast(s);
  });

  ws.begin();
  ws.onEvent([this](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_TEXT) {
      String msg = String((char*)payload).substring(0, length);
      handleMessage(num, msg);
    } else if (type == WStype_CONNECTED) {
      // Debounce rapid connect events from same client
      unsigned long now = millis();
      if (now - lastConnectMillis < 100) {
        // ignore very fast repeated connect events
        Serial.printf("[WS] Ignored spurious connect %u\n", num);
        lastConnectMillis = now;
        return;
      }
      lastConnectMillis = now;

      Serial.printf("[WS] Client %u connected (queued)\n", num);
      // enqueue for deferred initial send in loop()
      pendingClient = num;
      pendingClientTimestamp = millis();
      if (onClientConnected) {
        onClientConnected();
      }
    } else if (type == WStype_DISCONNECTED) {
      Serial.printf("[WS] Client %u disconnected\n", num);
      // if pendingClient was this client, clear
      if (pendingClient == num) pendingClient = -1;
      if (onClientDisconnected) {
        onClientDisconnected();
      }
    }
  });

  Serial.println("[WebSocket] Started on port 81");
}

void WebServerManager::loop() {
  ws.loop();

  // If we have a pending newly-connected client, send initial data after small delay
  if (pendingClient >= 0) {
    // wait a short time (50ms) to let client settle
    if (millis() - pendingClientTimestamp > 50) {
      uint8_t clientNum = (uint8_t)pendingClient;
      pendingClient = -1;

      // Prepare messages as lvalues
      String devs = deviceManager.getDevicesJson();
      String pins = deviceManager.getDetectedPinsJson();
      String st = deviceManager.getStatusJson();
      String boardInfo = deviceManager.getBoardInfoJson();

      // Send sequentially with tiny delays to avoid flooding
      ws.sendTXT(clientNum, devs);
      delay(10);
      ws.sendTXT(clientNum, pins);
      delay(10);
      ws.sendTXT(clientNum, st);
      delay(10);
      ws.sendTXT(clientNum, boardInfo);
      Serial.printf("[WS] Initial payloads sent to client %u\n", clientNum);
    }
  }

  // allow WebSocket internals to run
}

// broadcast safe wrapper: avoid re-entrancy floods
void WebServerManager::broadcast(const String& s) {
  if (busyBroadcast) {
    // skip if broadcast already in progress (simple protection)
    Serial.println("[WS] Broadcast busy - skipping");
    return;
  }
  busyBroadcast = true;
  String tmp = s;
  ws.broadcastTXT(tmp);
  busyBroadcast = false;
}

void WebServerManager::handleMessage(uint8_t num, String msg) {
  // parse JSON
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) {
    String errMsg = String("{\"error\":\"json_parse_failed\",\"msg\":\"") + err.c_str() + "\"}";
    // ensure lvalue
    String tmp = errMsg;
    ws.sendTXT(num, tmp);
    return;
  }

  String cmd = doc["cmd"] | "";
  if (cmd == "get_devices") {
    String out = deviceManager.getDevicesJson();
    ws.sendTXT(num, out);
  } else if (cmd == "get_status") {
    String out = deviceManager.getStatusJson();
    ws.sendTXT(num, out);
  } else if (cmd == "get_detected_pins") {
    String out = deviceManager.getDetectedPinsJson();
    ws.sendTXT(num, out);
  } else if (cmd == "set_config") {
    if (doc.containsKey("config") && doc["config"].containsKey("devices")) {
      deviceManager.clearDevices();
      JsonArray arr = doc["config"]["devices"].as<JsonArray>();
      for (JsonObject o : arr) {
        String name = o["name"].as<String>();
        String type = o["type"].as<String>();
        int pin1 = o.containsKey("pin") ? o["pin"].as<int>() : -1;
        int pin2 = o.containsKey("pin2") ? o["pin2"].as<int>() : -1;
        deviceManager.addDevice(name, pin1, pin2, type);
      }
      // save to SPIFFS
      deviceManager.saveToConfig(storage);
      String ok = String("{\"log\":\"config_saved\"}");
      ws.sendTXT(num, ok);
      // broadcast new devices list (use wrapper)
      String devs = deviceManager.getDevicesJson();
      broadcast(devs);
    } else {
      String errMsg = String("{\"error\":\"invalid_config\"}");
      ws.sendTXT(num, errMsg);
    }
  } else if (cmd == "action") {
    String name = doc["device"].as<String>();
    String act = doc["action"].as<String>();

    if (act == "on" || act == "off") {
      bool on = (act == "on");
      deviceManager.setDeviceState(name, on);
    }
    else if (act == "set_speed") {
      int speed = doc["value"] | 0;
      deviceManager.setDeviceSpeed(name, speed);
      Serial.printf("[WebServer] Motor %s speed=%d\n", name.c_str(), speed);
    }
    String logMsg = String("{\"log\":\"action_executed\",\"device\":\"") + name + "\"}";
    ws.sendTXT(num, logMsg);
    // broadcast device state updated (send devices array)
    String devs = deviceManager.getDevicesJson();
    broadcast(devs);
  } else if (cmd == "subscribe") {
    if (doc.containsKey("device")) {
      String name = doc["device"].as<String>();
      deviceManager.setSubscription(name, true);
      ws.sendTXT(num, "{\"log\":\"subscribed\"}");
    }
  } else if (cmd == "get_sensors") {
    String sensors = deviceManager.getSensorsJson();
    ws.sendTXT(num, sensors);
  } else {
    String errMsg = String("{\"error\":\"unknown_command\"}");
    ws.sendTXT(num, errMsg);
  }
}
