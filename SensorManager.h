#pragma once

#include "HapticMotor.h"
#include "ImuSensor.h"
#include "MicrophoneSensor.h"
#include "UltrasonicSensor.h"
#include "TransportService.h"

class SensorManager {
 public:
  explicit SensorManager(TransportService &transport) : transport_(transport) {}
  void begin();
  void poll();
  bool hapticBeep(uint8_t preset) { return hapticMotor_.beep(preset); }
  bool hapticPulse(uint8_t strength, uint32_t durationMs) {
    return hapticMotor_.pulse(strength, durationMs);
  }
  bool hapticStop() { return hapticMotor_.stop(); }
  bool hapticReady() const { return hapticMotor_.ready(); }
  bool hapticActive() const { return hapticMotor_.active(); }
  bool hapticFault() const { return hapticMotor_.fault(); }
  void hapticDebug() const { hapticMotor_.debug(); }

 private:
  bool buttonPressed();
  void startVoiceRecording();
  void stopVoiceRecording(const char *reason);
  void pollVoiceRecording();
  bool sendAudioChunk(const int16_t *samples, size_t sampleCount);
  void sendAudioEnd();
  bool mountMicroSd();
  bool tryMountMicroSd(uint8_t csPin, uint32_t frequencyHz);
  bool selectNextRecordingPath();
  void updateStatusLed(bool force = false);
  void writeStatusLed(bool on, bool force = false);
  void sendStatus();

  TransportService &transport_;
  ImuSensor imu_;
  UltrasonicSensor ultrasonic_;
  MicrophoneSensor microphone_;
  HapticMotor hapticMotor_;
  uint8_t *audioBuffer_ = nullptr;
  size_t audioBufferCapacity_ = 0;
  uint32_t lastStatusMs_ = 0;
  uint32_t lastSensorSerialStatusMs_ = 0;
  bool voiceRecording_ = false;
  bool savingAudio_ = false;
  bool recordingSendEnabled_ = false;
  bool recordingSaveEnabled_ = false;
  bool sdReady_ = false;
  uint8_t sdCsPin_ = 0xFF;
  bool ledUsable_ = true;
  bool ledOutputOn_ = false;
  uint32_t lastLedToggleMs_ = 0;
  bool buttonRawState_ = true;
  bool buttonStableState_ = true;
  uint32_t buttonChangedMs_ = 0;
  uint32_t recordingStartedMs_ = 0;
  uint32_t audioBytesWritten_ = 0;
  uint32_t nextAudioStreamId_ = 1;
  uint32_t audioStreamId_ = 0;
  uint32_t audioChunkIndex_ = 0;
  char audioFilePath_[32] = {};
};
