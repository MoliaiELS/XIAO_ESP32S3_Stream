#include "SensorManager.h"

#include <WiFi.h>
#include "Config.h"

void SensorManager::begin() {
  imu_.begin();
  tof_.begin();
  microphone_.begin();
}

void SensorManager::poll() {
  if (!transport_.connected()) return;

  WireProtocol::ImuPayload imuSample;
  if (imu_.read(imuSample)) {
    transport_.sendFrame(WireProtocol::MessageType::Imu, &imuSample, sizeof(imuSample));
  }

  WireProtocol::TofPayload tofSample;
  if (tof_.read(tofSample)) {
    transport_.sendFrame(WireProtocol::MessageType::Tof, &tofSample, sizeof(tofSample));
  }

  // Audio will be packetized as AudioPcmPrefix followed by PCM16 samples when enabled.
  if (millis() - lastStatusMs_ >= AppConfig::STATUS_INTERVAL_MS) sendStatus();
}

void SensorManager::sendStatus() {
  WireProtocol::StatusPayload status = {};
  status.uptimeMs = millis();
  status.freeHeapBytes = ESP.getFreeHeap();
  status.wifiRssiDbm = static_cast<int8_t>(WiFi.RSSI());
  status.sensorFlags = (imu_.available() ? 1u : 0u) |
                       (tof_.available() ? 2u : 0u) |
                       (microphone_.available() ? 4u : 0u);
  transport_.sendFrame(WireProtocol::MessageType::Status, &status, sizeof(status));
  lastStatusMs_ = millis();
}
