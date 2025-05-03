#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

const char* ssid = "Rafay";
const char* password = "12345678";

// MQTT broker config
const char* mqtt_server = "mqtt.flespi.io";
const int mqtt_port = 1883;
const char* mqtt_topic = "face/detected";
const char* mqtt_publish_topic = "door/closed";
const char* mqtt_client_id = "mqtt-subscriber-c55bcee3";
const char* mqtt_username = "d8MKDVUYFwUGNef1uUZGpurUfG6WKkJCFshWJRn6KelFQBa2dmNwBltrpyU35B2M";
const char* mqtt_password = "";

WiFiClient espClient;
PubSubClient client(espClient);
Servo myServo;

const int servoPin = 18;
bool doorOpened = false;
unsigned long openTime = 0;
const unsigned long delayDuration = 10000;  // 30 seconds

void setup_wifi() {
  delay(10);
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.printf("Message arrived [%s]: %s\n", topic, message.c_str());

  if (message == "true" && !doorOpened) {
    myServo.write(90);  // Move to 90°
    doorOpened = true;
    openTime = millis();  // Start 30s timer
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_client_id, mqtt_username, mqtt_password)) {
      Serial.println("connected");
      client.subscribe(mqtt_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  myServo.attach(servoPin);
  myServo.write(0); // Start at 0°
  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (doorOpened && millis() - openTime >= delayDuration) {
    myServo.write(0);  // Return to 0°
    doorOpened = false;

    // Publish door closed message
    bool success = client.publish(mqtt_publish_topic, "true");
    Serial.println(success ? "Published: door/closed = true" : "Failed to publish");
  }
}
