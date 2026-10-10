/*
 * =========================================================
 *  Robot Camera Test v3 — ESP32-S3 N16R8 + OV2640
 * =========================================================
 */

#include "esp_camera.h"
#include <WiFi.h>

const char* AP_SSID = "RobotCamera";
// Standalone diagnostic AP only; change before using outside a private bench.
const char* AP_PASS = "RobotCamera-Test-2026";

// ═══ Hãy thử đổi số này (1, 2, 3, 4) cho đến khi camera chạy! ═══
#define PIN_CONFIG 2

#if PIN_CONFIG == 1
// Config 1: Freenove WROVER
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y9_GPIO_NUM      11
#define Y8_GPIO_NUM       9
#define Y7_GPIO_NUM       8
#define Y6_GPIO_NUM      10
#define Y5_GPIO_NUM      12
#define Y4_GPIO_NUM      18
#define Y3_GPIO_NUM      17
#define Y2_GPIO_NUM      16
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13

#elif PIN_CONFIG == 2
// Config 2: Generic S3 N16R8 CAM (Rất phổ biến)
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y9_GPIO_NUM      16
#define Y8_GPIO_NUM      17
#define Y7_GPIO_NUM      18
#define Y6_GPIO_NUM      12
#define Y5_GPIO_NUM      10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM      11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13

#elif PIN_CONFIG == 3
// Config 3: Freenove / AI-Thinker S3 CAM mới
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    10
#define SIOD_GPIO_NUM    40
#define SIOC_GPIO_NUM    39
#define Y9_GPIO_NUM      48
#define Y8_GPIO_NUM      11
#define Y7_GPIO_NUM      12
#define Y6_GPIO_NUM      14
#define Y5_GPIO_NUM      16
#define Y4_GPIO_NUM      18
#define Y3_GPIO_NUM      17
#define Y2_GPIO_NUM      15
#define VSYNC_GPIO_NUM   38
#define HREF_GPIO_NUM    47
#define PCLK_GPIO_NUM    13

#elif PIN_CONFIG == 4
// Config 4: Seeed XIAO ESP32S3 Sense
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    10
#define SIOD_GPIO_NUM    40
#define SIOC_GPIO_NUM    39
#define Y9_GPIO_NUM      48
#define Y8_GPIO_NUM      11
#define Y7_GPIO_NUM      12
#define Y6_GPIO_NUM      14
#define Y5_GPIO_NUM      16
#define Y4_GPIO_NUM      18
#define Y3_GPIO_NUM      17
#define Y2_GPIO_NUM      15
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13
#endif

// ═════════════════════════════════════════════════════════

WiFiServer server(80);

bool initCamera() {
  camera_config_t config;
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sccb_sda  = SIOD_GPIO_NUM;
  config.pin_sccb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = RESET_GPIO_NUM;
  config.xclk_freq_hz  = 20000000;
  config.pixel_format  = PIXFORMAT_JPEG;

  if (psramFound()) {
    Serial.println("PSRAM: Co - dung VGA 640x480");
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;
  } else {
    Serial.println("PSRAM: Khong co - dung QVGA 320x240");
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera loi! Code: 0x%x\n", err);
    Serial.println("-> Hay doi PIN_CONFIG thanh so khac (1->4) roi nap lai!");
    return false;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Init OK nhung khong lay duoc frame!");
    Serial.println("-> Pin data bi sai! Hay doi PIN_CONFIG thanh so khac (1->4) roi nap lai!");
    esp_camera_deinit();
    return false;
  }
  Serial.printf("✅ Camera OK! %dx%d, %u bytes\n", fb->width, fb->height, fb->len);
  esp_camera_fb_return(fb);
  return true;
}

void handleStream(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Cache-Control: no-cache");
  client.println();

  uint32_t count = 0;
  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { delay(50); continue; }
    client.print("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ");
    client.print(fb->len);
    client.print("\r\n\r\n");
    client.write(fb->buf, fb->len);
    client.println();
    esp_camera_fb_return(fb);
    if (++count % 50 == 0) Serial.printf("%u frames\n", count);
  }
}

void handleSnapshot(WiFiClient& client) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    client.println("HTTP/1.1 500\r\n\r\nError");
    return;
  }
  client.println("HTTP/1.1 200 OK");
  client.printf("Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  Serial.println("Snapshot sent!");
}

void handleRoot(WiFiClient& client) {
  String html = "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'><title>Robot Cam</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{margin:0;background:#111;color:#eee;font-family:sans-serif;text-align:center;padding:10px}"
    "h2{color:#4fc3f7}"
    "img{max-width:100%;border:3px solid #4fc3f7;border-radius:8px}"
    "a{display:inline-block;margin:8px;padding:10px 20px;background:#0288d1;"
    "color:#fff;text-decoration:none;border-radius:20px}"
    "</style></head><body>"
    "<h2>Robot Camera Test</h2>"
    "<img src='/stream'><br>"
    "<a href='/snapshot' target='_blank'>Chup anh</a>"
    "<a href='/stream' target='_blank'>Tab moi</a>"
    "<p style='color:#888;font-size:.8em'>PIN_CONFIG: " + String(PIN_CONFIG) + "</p>"
    "</body></html>";

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=UTF-8");
  client.println("Connection: close");
  client.println();
  client.println(html);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Robot Camera Test v3 ===");
  Serial.printf("Dang thu PIN_CONFIG = %d\n\n", PIN_CONFIG);

  if (!initCamera()) {
    Serial.println("⛔ Dung hoat dong. Hay thu config khac!");
    while (true) delay(1000);
  }

  Serial.printf("Dang tao WiFi hotspot: %s ...\n", AP_SSID);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(500);

  Serial.println("\n=== Hotspot OK! ===");
  Serial.printf("Ket noi WiFi: \"%s\"\n", AP_SSID);
  Serial.printf("Mo trinh duyet: http://%s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("===================");

  server.begin();
}

void loop() {
  WiFiClient client = server.accept();
  if (!client) return;

  String req = "";
  unsigned long t = millis() + 2000;
  while (client.connected() && millis() < t) {
    if (client.available()) {
      String line = client.readStringUntil('\r');
      if (req.isEmpty()) req = line;
      if (line.length() <= 1) break;
    }
  }

  if (req.indexOf("/stream") >= 0)       handleStream(client);
  else if (req.indexOf("/snapshot") >= 0) handleSnapshot(client);
  else                                    handleRoot(client);

  client.stop();
}
