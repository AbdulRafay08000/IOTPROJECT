#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>

// Camera model
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// WiFi credentials
const char *ssid = "Rafay";
const char *password = "12345678";

// MQTT Settings
const char* mqtt_server = "mqtt.flespi.io";
const int mqtt_port = 1883;
const char* mqtt_username = "d8MKDVUYFwUGNef1uUZGpurUfG6WKkJCFshWJRn6KelFQBa2dmNwBltrpyU35B2M";
const char* mqtt_password = "";
const char* mqtt_client_id = "mqtt-board-c55bcee3";

// Topics
const char* mqtt_status_topic = "camera/status";
const char* mqtt_metadata_topic = "camera/metadata";
const char* mqtt_frame_end_topic = "camera/frame/end";
const char* mqtt_chunk_topic_prefix = "camera/chunk/";

// Settings
const int chunk_size = 2048;
const int frame_interval = 500;  // Limit to 2 fps
unsigned long last_frame_time = 0;
unsigned long lastReconnectAttempt = 0;
const int reconnectInterval = 5000;

WiFiClient espClient;
PubSubClient client(espClient);

void callback(char* topic, byte* payload, unsigned int length) {
  // Empty for now
}

bool connectMQTT() {
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setKeepAlive(60);
  client.setSocketTimeout(60);
  client.setBufferSize(chunk_size + 100);

  Serial.print("Attempting MQTT connection... ");
  if (client.connect(mqtt_client_id, mqtt_username, mqtt_password, mqtt_status_topic, 1, true, "offline")) {
    Serial.println("connected");
    client.publish(mqtt_status_topic, "online", true);
    return true;
  } else {
    Serial.print("failed, rc=");
    Serial.println(client.state());
    return false;
  }
}

void ensureWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("WiFi reconnecting to ");
    Serial.println(ssid);

    WiFi.disconnect();
    WiFi.begin(ssid, password);
    WiFi.setSleep(false);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500);
      Serial.print(".");
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("\nWiFi connection failed.");
    }
  }
}

void publishCameraFrame() {
  if (!client.connected()) return;

  unsigned long now = millis();
  if (now - last_frame_time < frame_interval) return;
  last_frame_time = now;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  char metadata[64];
  sprintf(metadata, "%d,%d,%d", fb->len, fb->width, fb->height);

  if (!client.publish(mqtt_metadata_topic, metadata, false)) {
    Serial.println("Failed to publish metadata");
    esp_camera_fb_return(fb);
    return;
  }

  int num_chunks = (fb->len + chunk_size - 1) / chunk_size;
  int success = 0;

  for (int i = 0; i < num_chunks; i++) {
    int offset = i * chunk_size;
    int size = (offset + chunk_size > fb->len) ? (fb->len - offset) : chunk_size;

    char chunk_topic[32];
    sprintf(chunk_topic, "%sc%d", mqtt_chunk_topic_prefix, i);

    if (client.publish(chunk_topic, fb->buf + offset, size, false)) {
      success++;
    } else {
      Serial.printf("Failed chunk %d\n", i);
    }

    client.loop(); // Keep connection alive
    delay(15);     // Prevent overload
  }

  if (success == num_chunks) {
    client.publish(mqtt_frame_end_topic, "done", false);
    Serial.printf("Published frame: %d bytes in %d chunks\n", fb->len, num_chunks);
  } else {
    Serial.println("Frame incomplete, some chunks failed");
  }

  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println();

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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.println("Connecting to WiFi...");
  
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
    connectMQTT();
  } else {
    Serial.println("\nWiFi connection failed.");
  }
}

void loop() {
  ensureWiFiConnection();

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      unsigned long now = millis();
      if (now - lastReconnectAttempt > reconnectInterval) {
        lastReconnectAttempt = now;
        if (connectMQTT()) {
          lastReconnectAttempt = 0;
        }
      }
    } else {
      client.loop();
      publishCameraFrame();
    }
  }

  delay(10);  // Watchdog feed
}
