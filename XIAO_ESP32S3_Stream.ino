#include <Arduino.h>
#include <WiFi.h>

#include "CameraService.h"
#include "CommandHandler.h"
#include "Config.h"
#include "SensorManager.h"
#include "TransportService.h"

CameraService cameraService;
TransportService transportService(AppConfig::DATA_PORT);
SensorManager sensorManager(transportService);

namespace {
bool networkServicesStarted = false;
uint32_t lastWifiAttemptMs = 0;

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(AppConfig::HOSTNAME);
  WiFi.setSleep(!AppConfig::WIFI_MAX_THROUGHPUT);
  WiFi.begin(AppConfig::WIFI_SSID, AppConfig::WIFI_PASSWORD);
  lastWifiAttemptMs = millis();
  Serial.printf("Connecting to Wi-Fi '%s'", AppConfig::WIFI_SSID);
}

void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!networkServicesStarted) {
      transportService.begin();
      if (!cameraService.beginServer(AppConfig::VIDEO_PORT)) {
        Serial.println("Failed to start the video server");
      }
      networkServicesStarted = true;

      const String ip = WiFi.localIP().toString();
      Serial.printf("\nWi-Fi connected: %s\n", ip.c_str());
      Serial.printf("Video: http://%s:%u/stream\n", ip.c_str(), AppConfig::VIDEO_PORT);
      Serial.printf("Data/commands: %s:%u (TCP)\n", ip.c_str(), AppConfig::DATA_PORT);
    }
    return;
  }

  if (millis() - lastWifiAttemptMs >= AppConfig::WIFI_RETRY_MS) {
    Serial.println("\nWi-Fi disconnected; reconnecting...");
    WiFi.disconnect();
    WiFi.begin(AppConfig::WIFI_SSID, AppConfig::WIFI_PASSWORD);
    lastWifiAttemptMs = millis();
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nXIAO ESP32S3 Sense wearable stream starting");

  if (!cameraService.beginCamera()) {
    Serial.println("Camera unavailable; data/command channel will still start");
  }

  sensorManager.begin();
  connectWifi();
}

void loop() {
  maintainWifi();

  if (networkServicesStarted && WiFi.status() == WL_CONNECTED) {
    transportService.poll();

    ReceivedFrame frame;
    if (transportService.popReceived(frame)) {
      CommandHandler::handle(frame, transportService);
    }
  }

  // Always poll local sensors/button so SD recording does not depend on Wi-Fi.
  sensorManager.poll();
  delay(1);
}
