#pragma once

#include "ImuSensor.h"
#include "MicrophoneSensor.h"
#include "TofSensor.h"
#include "TransportService.h"

class SensorManager {
 public:
  explicit SensorManager(TransportService &transport) : transport_(transport) {}
  void begin();
  void poll();

 private:
  void sendStatus();

  TransportService &transport_;
  ImuSensor imu_;
  TofSensor tof_;
  MicrophoneSensor microphone_;
  uint32_t lastStatusMs_ = 0;
};
