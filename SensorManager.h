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
  void stopVoiceRecording(const char *reason, bool sendEndFrame = true);
  void pollVoiceRecording();
  void sendStatus();

  TransportService &transport_;
  ImuSensor imu_;
  TofSensor tof_;
  MicrophoneSensor microphone_;
  uint32_t lastStatusMs_ = 0;
  bool voiceRecording_ = false;
  bool buttonRawState_ = true;
  bool buttonStableState_ = true;
  bool speechDetected_ = false;
  uint32_t buttonChangedMs_ = 0;
  uint32_t recordingStartedMs_ = 0;
  uint32_t lastSpeechMs_ = 0;
  uint32_t nextAudioStreamId_ = 1;
  uint32_t audioStreamId_ = 0;
  uint32_t audioChunkIndex_ = 0;
};
