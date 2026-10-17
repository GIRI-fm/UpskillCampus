/*
 * Air Pollution Monitoring System
 * ESP32 + MQ-135 + DHT11 + Wi-Fi/MQTT
 *
 * Libraries:
 *   - PubSubClient by Nick O'Leary
 *   - DHT sensor library by Adafruit
 *   - Adafruit Unified Sensor
 *
 * Hardware:
 *   MQ-135 analog output -> ESP32 GPIO 34
 *   DHT11 data           -> ESP32 GPIO 15
 *   Active buzzer (+)    -> ESP32 GPIO 18 (optional; use a suitable driver if needed)
 *
 * IMPORTANT:
 * MQ-135 is a broad gas/air-quality sensor. Raw ADC values are not certified AQI
 * or ppm values. Calibrate the sensor for the target gas and environment before
 * making quantitative claims. Warm-up time is required.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"

#define MQ135_PIN 34
#define DHT_PIN 15
#define DHT_TYPE DHT11
#define BUZZER_PIN 18

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_SERVER = "broker.hivemq.com";
const int MQTT_PORT = 1883;

// Example threshold only: tune after observing/calibrating your own sensor.
const int AIR_QUALITY_ALERT_THRESHOLD = 1800;
const unsigned long PUBLISH_INTERVAL_MS = 5000;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
DHT dht(DHT_PIN, DHT_TYPE);

unsigned long lastPublish = 0;

void connectWiFi() {
  Serial.print("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Wi-Fi connected. IP address: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT broker... ");
    String clientId = "AirPollutionESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, state=");
      Serial.print(mqttClient.state());
      Serial.println("; retrying in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(MQ135_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  connectWiFi();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setBufferSize(256);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = now;

    int airRaw = analogRead(MQ135_PIN);
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    bool airAlert = (airRaw >= AIR_QUALITY_ALERT_THRESHOLD);
    digitalWrite(BUZZER_PIN, airAlert ? HIGH : LOW);

    Serial.print("MQ-135 raw ADC: ");
    Serial.println(airRaw);

    if (!isnan(temperature) && !isnan(humidity)) {
      Serial.print("Temperature (C): ");
      Serial.println(temperature, 1);
      Serial.print("Humidity (%): ");
      Serial.println(humidity, 1);

      char tempPayload[16];
      char humidityPayload[16];
      dtostrf(temperature, 1, 1, tempPayload);
      dtostrf(humidity, 1, 1, humidityPayload);

      mqttClient.publish("airpollution/temperature_c", tempPayload);
      mqttClient.publish("airpollution/humidity_percent", humidityPayload);
    } else {
      Serial.println("DHT11 read failed; check wiring and sensor.");
    }

    char airPayload[12];
    itoa(airRaw, airPayload, 10);
    mqttClient.publish("airpollution/mq135_raw_adc", airPayload);
    mqttClient.publish("airpollution/alert", airAlert ? "HIGH" : "NORMAL");

    Serial.print("Air-quality status: ");
    Serial.println(airAlert ? "HIGH (threshold exceeded)" : "NORMAL");
  }
}
