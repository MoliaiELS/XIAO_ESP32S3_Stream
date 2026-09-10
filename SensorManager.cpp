#include "SensorManager.h"

#include <WiFi.h>

#include "Config.h"

namespace {
constexpr size_t AUDIO_CHUNK_SAMPLES =
    (AppConfig::AUDIO_SAMPLE_RATE_HZ * AppConfig::AUDIO_CHUNK_MS) / 1000;

struct AudioPacket {
  WireProtocol::AudioPcmPrefix prefix;
  int16_t samples[AUDIO_CHUNK_SAMPLES];
};

bool isVoiceActive(const int16_t *samples, size_t count) {
  if (count == 0) return false;
  uint64_t sumSquares = 0;
  for (size_t i = 0; i < count; ++i) {
    const int32_t sample = samples[i];
    sumSquares += static_cast<uint64_t>(sample * sample);
  }
  const uint64_t threshold = AppConfig::AUDIO_VAD_RMS_THRESHOLD;
  return (sumSquares / count) >= (threshold * threshold);
}
}  // namespace

void SensorManager::begin() {
  imu_.begin();
  tof_.begin();
  microphone_.begin();

  pinMode(AppConfig::VOICE_BUTTON_PIN,
          AppConfig::VOICE_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  buttonRawState_ = digitalRead(AppConfig::VOICE_BUTTON_PIN);
  buttonStableState_ = buttonRawState_;
  buttonChangedMs_ = millis();
  Serial.printf("Voice button ready on GPIO%u\n", AppConfig::VOICE_BUTTON_PIN);
}

void SensorManager::poll() {
  if (buttonPressed()) {
    if (voiceRecording_) {
      stopVoiceRecording("button pressed");
    } else {
      startVoiceRecording();
    }
  }

  if (voiceRecording_ && !transport_.connected()) {
    stopVoiceRecording("TCP client disconnected", false);
  }
  if (!transport_.connected()) return;

  if (voiceRecording_) pollVoiceRecording();

  WireProtocol::ImuPayload imuSample;
  if (imu_.read(imuSample)) {
    transport_.sendFrame(WireProtocol::MessageType::Imu, &imuSample, sizeof(imuSample));
  }

  WireProtocol::TofPayload tofSample;
  if (tof_.read(tofSample)) {
    transport_.sendFrame(WireProtocol::MessageType::Tof, &tofSample, sizeof(tofSample));
  }

  if (millis() - lastStatusMs_ >= AppConfig::STATUS_INTERVAL_MS) sendStatus();
}

bool SensorManager::buttonPressed() {
  const bool rawState = digitalRead(AppConfig::VOICE_BUTTON_PIN);
  const uint32_t now = millis();
  if (rawState != buttonRawState_) {
    buttonRawState_ = rawState;
    buttonChangedMs_ = now;
  }

  if (now - buttonChangedMs_ < AppConfig::BUTTON_DEBOUNCE_MS ||
      buttonStableState_ == buttonRawState_) {
    return false;
  }

  buttonStableState_ = buttonRawState_;
  const bool activeState = AppConfig::VOICE_BUTTON_ACTIVE_LOW ? LOW : HIGH;
  return buttonStableState_ == activeState;
}

void SensorManager::startVoiceRecording() {
  if (!transport_.connected()) {
    Serial.println("Cannot record: no laptop TCP client connected");
    return;
  }
  if (!microphone_.available() || !microphone_.startCapture()) {
    Serial.println("Cannot record: microphone unavailable");
    return;
  }

  voiceRecording_ = true;
  speechDetected_ = false;
  recordingStartedMs_ = millis();
  lastSpeechMs_ = recordingStartedMs_;
  audioStreamId_ = nextAudioStreamId_++;
  audioChunkIndex_ = 0;
  Serial.printf("Voice recording started (stream %lu)\n",
                static_cast<unsigned long>(audioStreamId_));
}

void SensorManager::stopVoiceRecording(const char *reason, bool sendEndFrame) {
  if (!voiceRecording_) return;

  if (sendEndFrame && transport_.connected()) {
    WireProtocol::AudioPcmPrefix end = {};
    end.streamId = audioStreamId_;
    end.chunkIndex = audioChunkIndex_;
    end.sampleRateHz = AppConfig::AUDIO_SAMPLE_RATE_HZ;
    end.sampleCount = 0;
    end.channels = 1;
    end.bitsPerSample = 16;
    transport_.sendFrame(WireProtocol::MessageType::AudioPcm, &end, sizeof(end),
                         WireProtocol::FLAG_STREAM_END);
  }

  microphone_.stopCapture();
  voiceRecording_ = false;
  Serial.printf("Voice recording stopped: %s\n", reason);
}

void SensorManager::pollVoiceRecording() {
  AudioPacket packet = {};
  const size_t samplesRead =
      microphone_.readPcm16(packet.samples, AUDIO_CHUNK_SAMPLES);
  if (samplesRead == 0) {
    stopVoiceRecording("microphone read failed");
    return;
  }

  packet.prefix.streamId = audioStreamId_;
  packet.prefix.chunkIndex = audioChunkIndex_;
  packet.prefix.sampleRateHz = AppConfig::AUDIO_SAMPLE_RATE_HZ;
  packet.prefix.sampleCount = static_cast<uint16_t>(samplesRead);
  packet.prefix.channels = 1;
  packet.prefix.bitsPerSample = 16;

  uint16_t flags = WireProtocol::FLAG_MORE_FRAGMENTS;
  if (audioChunkIndex_ == 0) flags |= WireProtocol::FLAG_STREAM_START;
  const size_t payloadLength =
      sizeof(packet.prefix) + samplesRead * sizeof(packet.samples[0]);
  if (!transport_.sendFrame(WireProtocol::MessageType::AudioPcm, &packet,
                            payloadLength, flags)) {
    stopVoiceRecording("audio send failed", false);
    return;
  }
  ++audioChunkIndex_;

  const uint32_t now = millis();
  if (isVoiceActive(packet.samples, samplesRead)) {
    speechDetected_ = true;
    lastSpeechMs_ = now;
  }

  if (now - recordingStartedMs_ >= AppConfig::AUDIO_MAX_RECORDING_MS) {
    stopVoiceRecording("maximum duration reached");
  } else if (!speechDetected_ &&
             now - recordingStartedMs_ >= AppConfig::AUDIO_WAIT_FOR_SPEECH_MS) {
    stopVoiceRecording("no speech detected");
  } else if (speechDetected_ &&
             now - lastSpeechMs_ >= AppConfig::AUDIO_SILENCE_TIMEOUT_MS) {
    stopVoiceRecording("silence detected");
  }
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
