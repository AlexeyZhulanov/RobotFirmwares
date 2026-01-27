#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ================= НАСТРОЙКИ СЕТИ =================
const char* ssid = "Biznes";
const char* password = "12345679";

#define STREAM_PORT 81
#define UDP_PORT 4210

WiFiServer server(STREAM_PORT);
WiFiUDP udp;

// ================= ПИНЫ AI THINKER =================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ================= MJPEG =================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

bool clientStreaming = false;

// ================= FPS =================
#define TARGET_FPS 30
#define FRAME_INTERVAL_MS (1000 / TARGET_FPS)

void handleStream(WiFiClient& client);

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // ===== Камера =====
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 20; // чем больше, тем хуже качество
    config.fb_count = 2;
    Serial.println("PSRAM: OK");
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 20;
    config.fb_count = 1;
    Serial.println("PSRAM: NO");
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    return;
  }

  // ===== WiFi =====
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.printf("Camera Stream Ready: http://%s:%d\n",
                WiFi.localIP().toString().c_str(), STREAM_PORT);

  server.begin();
  udp.begin(UDP_PORT);
}

void loop() {
  // ===== UDP DISCOVERY =====
  static unsigned long lastSend = 0;
  if (!clientStreaming && millis() - lastSend > 3000) {
    udp.beginPacket(WiFi.gatewayIP(), UDP_PORT);
    udp.print("I_AM_CAMERA:");
    udp.print(WiFi.localIP().toString());
    udp.endPacket();
    lastSend = millis();
  }

  // ===== STREAM =====
  WiFiClient client = server.available();
  if (client && !clientStreaming) {
    clientStreaming = true;
    handleStream(client);
    clientStreaming = false;
  }

  delay(1); // watchdog-safe
}

void handleStream(WiFiClient& client) {
  Serial.println("[STREAM] Client connected");

  client.setNoDelay(true);
  client.setTimeout(5);

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: " + String(_STREAM_CONTENT_TYPE));
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();

  unsigned long lastFrameTime = 0;

  while (client.connected()) {
    if (millis() - lastFrameTime < FRAME_INTERVAL_MS) {
      delay(1);
      continue;
    }
    lastFrameTime = millis();

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) break;

    client.print(_STREAM_BOUNDARY);
    client.printf(_STREAM_PART, fb->len);

    size_t sent = client.write(fb->buf, fb->len);
    client.print("\r\n");

    esp_camera_fb_return(fb);

    if (sent != fb->len) {
      Serial.println("[STREAM] Incomplete frame sent");
      break;
    }
  }

  client.stop();
  Serial.println("[STREAM] Client disconnected");
}
