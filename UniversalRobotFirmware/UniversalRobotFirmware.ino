#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>

#include "ConfigStorage.h"
#include "DeviceManager.h"
#include "RS485Manager.h"
#include "WebServerManager.h"

ConfigStorage configStorage("/config.json");
DeviceManager deviceManager;
WebServerManager wsManager(81, deviceManager, configStorage);

#define RS485_RX_PIN D6
#define RS485_TX_PIN D7
RS485Manager rs485Manager(RS485_RX_PIN, RS485_TX_PIN);

ESP8266WebServer httpServer(80);

const char* ssid = "Biznes";     // Имя сети телефона
const char* password = "12345679";   // Пароль

WiFiUDP udp;
unsigned int udpPort = 4210; // порт для поиска
char packetBuffer[255]; // буфер для приема
bool discoveryDone = false; // флаг чтобы не слать пинги вечно

void setup() {
  // Встроенный светодиод
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[SETUP] Universal Robot Firmware starting...");

  // SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("[ERROR] SPIFFS.begin() failed");
  } else {
    Serial.println("[FS] SPIFFS mounted");
  }

  // Включаем режим клиента
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Ждем подключения (важно!)
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected!");
  Serial.printf("UDP Discovery started on port %d\n", udpPort);
  Serial.print("[WiFi] ESP IP: ");
  Serial.println(WiFi.localIP());

  udp.begin(udpPort);

  // HTTP server (simple status endpoint; OTA endpoint can be added)
  httpServer.on("/", []() {
    httpServer.send(200, "text/plain", "RobotBoard online (WebSocket on port 81)");
  });
  httpServer.begin();

  // Initialize device manager (load config if exists)
  deviceManager.setRS485Manager(&rs485Manager);
  deviceManager.loadFromConfig(configStorage); // Загружаем сохраненные устройства

  // Start RS485 manager
  rs485Manager.begin();

  // Start WebSocket manager (it will set broadcaster lambda into deviceManager)
  wsManager.begin();
  wsManager.onClientConnected = []() {
    discoveryDone = true;
    Serial.println("[UDP] Discovery stopped (WS connected)");
  };
  wsManager.onClientDisconnected = []() {
    discoveryDone = false;
    Serial.println("[UDP] Discovery resumed (WS disconnected)");
  };
  Serial.println("[INIT] Ready.");
}

void loop() {
  httpServer.handleClient();
  wsManager.loop();
  // --- UDP DISCOVERY ---
  static unsigned long lastSend = 0;
  if (!discoveryDone && millis() - lastSend > 3000) {
    sendDiscovery();
    lastSend = millis();
  }
  // ---------------------
  deviceManager.loop();
  rs485Manager.loop();
}

void sendDiscovery() {
  IPAddress phoneIp = WiFi.gatewayIP();

  udp.beginPacket(phoneIp, 4210);
  udp.print("I_AM_ROBOT:");
  udp.print(WiFi.localIP().toString()); // передаём IP ESP
  udp.endPacket();

  Serial.println("[UDP] Discovery sent to phone");
}
