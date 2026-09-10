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
  bool buttonPressed();
  void startVoiceRecording();
  void stopVoiceRecording(const char *reason);
  void pollVoiceRecording();
  bool mountMicroSd();
  bool tryMountMicroSd(uint8_t csPin, uint32_t frequencyHz);
  bool selectNextRecordingPath();
  void sendStatus();

  TransportService &transport_;
  ImuSensor imu_;
  TofSensor tof_;
  MicrophoneSensor microphone_;
  uint8_t *audioBuffer_ = nullptr;
  size_t audioBufferCapacity_ = 0;
  uint32_t lastStatusMs_ = 0;
  bool voiceRecording_ = false;
  bool sdReady_ = false;
  uint8_t sdCsPin_ = 0xFF;
  bool buttonRawState_ = true;
  bool buttonStableState_ = true;
  uint32_t buttonChangedMs_ = 0;
  uint32_t recordingStartedMs_ = 0;
  uint32_t audioBytesWritten_ = 0;
  char audioFilePath_[32] = {};
};
