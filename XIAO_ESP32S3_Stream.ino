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
bool wifiWasConnected = false;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastWifiStatusMs = 0;

void printWifiDetails() {
  const String ip = WiFi.localIP().toString();
  Serial.println("[WiFi] Connection details:");
  Serial.printf("[WiFi] SSID=%s, IP=%s, RSSI=%d dBm, channel=%d\n",
                WiFi.SSID().c_str(), ip.c_str(), WiFi.RSSI(), WiFi.channel());
  Serial.printf("[WiFi] gateway=%s, subnet=%s, DNS=%s, MAC=%s\n",
                WiFi.gatewayIP().toString().c_str(),
                WiFi.subnetMask().toString().c_str(),
                WiFi.dnsIP().toString().c_str(), WiFi.macAddress().c_str());
  Serial.printf("[WiFi] Video: http://%s:%u/stream\n", ip.c_str(),
                AppConfig::VIDEO_PORT);
  Serial.printf("[WiFi] Data/commands/audio: %s:%u (TCP)\n", ip.c_str(),
                AppConfig::DATA_PORT);
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(AppConfig::HOSTNAME);
  WiFi.setSleep(!AppConfig::WIFI_MAX_THROUGHPUT);
  WiFi.begin(AppConfig::WIFI_SSID, AppConfig::WIFI_PASSWORD);
  lastWifiAttemptMs = millis();
  Serial.printf("[WiFi] Connecting to '%s' as '%s'\n", AppConfig::WIFI_SSID,
                AppConfig::HOSTNAME);
}

void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!networkServicesStarted) {
      transportService.begin();
      if (!cameraService.beginServer(AppConfig::VIDEO_PORT)) {
        Serial.println("Failed to start the video server");
      }
      networkServicesStarted = true;
    }

    if (!wifiWasConnected) {
      Serial.println("[WiFi] Connected");
      printWifiDetails();
      wifiWasConnected = true;
      lastWifiStatusMs = millis();
    } else if (millis() - lastWifiStatusMs >=
               AppConfig::WIFI_STATUS_INTERVAL_MS) {
      Serial.printf("[WiFi] Online: IP=%s, RSSI=%d dBm, channel=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                    WiFi.channel());
      lastWifiStatusMs = millis();
    }
    return;
  }

  if (wifiWasConnected) {
    Serial.printf("[WiFi] Connection lost (status=%d); status LED is off\n",
                  static_cast<int>(WiFi.status()));
    wifiWasConnected = false;
  }

  if (millis() - lastWifiAttemptMs >= AppConfig::WIFI_RETRY_MS) {
    Serial.printf("[WiFi] Still offline (status=%d); reconnecting to '%s'...\n",
                  static_cast<int>(WiFi.status()), AppConfig::WIFI_SSID);
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
      CommandHandler::handle(frame, transportService, sensorManager);
    }
  }

  // Always poll local sensors/button so SD recording does not depend on Wi-Fi.
  sensorManager.poll();
  delay(1);
}
