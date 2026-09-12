#include "SensorManager.h"

#include <SD.h>
#include <SPI.h>
#include <WiFi.h>

#include "Config.h"

namespace {
constexpr size_t AUDIO_CHUNK_SAMPLES =
    (AppConfig::AUDIO_SAMPLE_RATE_HZ * AppConfig::AUDIO_CHUNK_MS) / 1000;
constexpr size_t AUDIO_BUFFER_BYTES =
    (AppConfig::AUDIO_SAMPLE_RATE_HZ * sizeof(int16_t) *
     AppConfig::AUDIO_MAX_RECORDING_MS) /
    1000;

struct AudioPacket {
  WireProtocol::AudioPcmPrefix prefix;
  int16_t samples[AUDIO_CHUNK_SAMPLES];
};

// Standard 44-byte PCM WAV header. ESP32 is little-endian, matching WAV.
#pragma pack(push, 1)
struct WavHeader {
  char riff[4];
  uint32_t riffSize;
  char wave[4];
  char fmt[4];
  uint32_t fmtSize;
  uint16_t audioFormat;
  uint16_t channels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  char data[4];
  uint32_t dataSize;
};
#pragma pack(pop)

static_assert(sizeof(WavHeader) == 44, "Unexpected WAV header size");

bool writeWavHeader(File &file, uint32_t pcmBytes) {
  const uint32_t byteRate = AppConfig::AUDIO_SAMPLE_RATE_HZ * sizeof(int16_t);
  const WavHeader header = {
      {'R', 'I', 'F', 'F'}, pcmBytes + 36, {'W', 'A', 'V', 'E'},
      {'f', 'm', 't', ' '}, 16,            1,
      1,                   AppConfig::AUDIO_SAMPLE_RATE_HZ,
      byteRate,            sizeof(int16_t), 16,
      {'d', 'a', 't', 'a'}, pcmBytes,
  };
  return file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) ==
         sizeof(header);
}

}  // namespace

void SensorManager::begin() {
  if (AppConfig::ENABLE_IMU) imu_.begin();
  if (AppConfig::ENABLE_ULTRASONIC) ultrasonic_.begin();
  microphone_.begin();
  if (AppConfig::ENABLE_HAPTIC_MOTOR) {
    hapticMotor_.begin(AppConfig::HAPTIC_SDA_PIN, AppConfig::HAPTIC_SCL_PIN,
                       AppConfig::HAPTIC_PEAK_VOLTS,
                       AppConfig::HAPTIC_FREQUENCY_HZ);
  } else {
    Serial.println("[HAPTIC] Disabled in Config.h");
  }

  pinMode(AppConfig::VOICE_BUTTON_PIN,
          AppConfig::VOICE_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  buttonRawState_ = digitalRead(AppConfig::VOICE_BUTTON_PIN);
  buttonStableState_ = buttonRawState_;
  buttonChangedMs_ = millis();
  Serial.printf("Voice button ready on GPIO%u\n", AppConfig::VOICE_BUTTON_PIN);

  pinMode(AppConfig::RECORDING_LED_PIN, OUTPUT);
  writeStatusLed(false, true);
  Serial.printf("Status LED ready on GPIO%u (active low): Wi-Fi=solid, "
                "record/save=fast blink\n",
                AppConfig::RECORDING_LED_PIN);

  if (AppConfig::ENABLE_SD_AUDIO_RECORDING) {
    sdReady_ = mountMicroSd();
    if (sdReady_) {
      const char *cardType = "UNKNOWN";
      if (SD.cardType() == CARD_MMC) cardType = "MMC";
      if (SD.cardType() == CARD_SD) cardType = "SDSC";
      if (SD.cardType() == CARD_SDHC) cardType = "SDHC";
      Serial.printf("microSD ready: type=%s, size=%llu MB, CS=GPIO%u\n",
                    cardType, SD.cardSize() / (1024ULL * 1024ULL),
                    sdCsPin_);
      if (sdCsPin_ == AppConfig::RECORDING_LED_PIN) {
        ledUsable_ = false;
        digitalWrite(AppConfig::RECORDING_LED_PIN, HIGH);
        Serial.printf("Status LED disabled: GPIO%u is being used as microSD CS\n",
                      AppConfig::RECORDING_LED_PIN);
      }
    } else {
      Serial.println("microSD mount failed on both known CS pins");
      Serial.println("Check card insertion, FAT32 format, B2B connector and J3");
    }
  } else {
    Serial.println("Local microSD audio saving is disabled in Config.h");
  }

  Serial.printf("Laptop audio streaming: %s\n",
                AppConfig::ENABLE_AUDIO_TCP_STREAM ? "enabled" : "disabled");
  if (AppConfig::ENABLE_SD_AUDIO_RECORDING ||
      AppConfig::ENABLE_AUDIO_TCP_STREAM) {
    Serial.println("Press BOOT to start/stop audio recording");
  } else {
    Serial.println("BOOT audio recording is disabled in Config.h");
  }
}

bool SensorManager::mountMicroSd() {
  const uint8_t candidates[] = {AppConfig::SD_CS_PIN,
                                AppConfig::SD_FALLBACK_CS_PIN};
  for (const uint8_t csPin : candidates) {
    if (tryMountMicroSd(csPin, AppConfig::SD_SPI_FREQUENCY_HZ)) return true;
    if (tryMountMicroSd(csPin, 1000000)) return true;
  }
  return false;
}

bool SensorManager::tryMountMicroSd(uint8_t csPin, uint32_t frequencyHz) {
  SD.end();
  SPI.end();
  delay(20);

  pinMode(csPin, OUTPUT);
  digitalWrite(csPin, HIGH);
  SPI.begin(AppConfig::SD_SCK_PIN, AppConfig::SD_MISO_PIN,
            AppConfig::SD_MOSI_PIN, csPin);
  Serial.printf("Trying microSD: CS=GPIO%u, SPI=%lu Hz... ", csPin,
                static_cast<unsigned long>(frequencyHz));

  if (!SD.begin(csPin, SPI, frequencyHz) || SD.cardType() == CARD_NONE) {
    Serial.println("failed");
    return false;
  }

  sdCsPin_ = csPin;
  Serial.println("mounted");
  return true;
}

void SensorManager::poll() {
  imu_.poll();
  ultrasonic_.poll();
  hapticMotor_.update();
  updateStatusLed();

  const uint32_t now = millis();
  if (uint32_t(now - lastSensorSerialStatusMs_) >=
      AppConfig::SENSOR_SERIAL_STATUS_INTERVAL_MS) {
    if (AppConfig::ENABLE_IMU) imu_.debug();
    if (AppConfig::ENABLE_ULTRASONIC) ultrasonic_.debug();
    if (AppConfig::ENABLE_HAPTIC_MOTOR) hapticMotor_.debug();
    lastSensorSerialStatusMs_ = now;
  }

  if (AppConfig::ENABLE_SD_AUDIO_RECORDING ||
      AppConfig::ENABLE_AUDIO_TCP_STREAM) {
    if (buttonPressed()) {
      if (voiceRecording_) {
        stopVoiceRecording("button pressed");
      } else {
        startVoiceRecording();
      }
    }
    if (voiceRecording_) pollVoiceRecording();
  }

  updateStatusLed();

  if (!transport_.connected()) return;

  WireProtocol::ImuPayload imuSample;
  if (imu_.read(imuSample)) {
    transport_.sendFrame(WireProtocol::MessageType::Imu, &imuSample, sizeof(imuSample));
  }

  WireProtocol::UltrasonicPayload ultrasonicSample;
  if (ultrasonic_.read(ultrasonicSample)) {
    transport_.sendFrame(WireProtocol::MessageType::Ultrasonic,
                         &ultrasonicSample, sizeof(ultrasonicSample));
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
  Serial.println("[REC] Start requested by BOOT button");
  audioFilePath_[0] = '\0';
  recordingSaveEnabled_ = AppConfig::ENABLE_SD_AUDIO_RECORDING && sdReady_;
  recordingSendEnabled_ =
      AppConfig::ENABLE_AUDIO_TCP_STREAM && transport_.connected();

  if (AppConfig::ENABLE_SD_AUDIO_RECORDING && !sdReady_) {
    Serial.println("[REC/SD] Skipped: microSD card is unavailable");
  }
  if (AppConfig::ENABLE_AUDIO_TCP_STREAM && !transport_.connected()) {
    Serial.println("[REC/TCP] Skipped: laptop data client is not connected");
  }

  if (!recordingSaveEnabled_ && !recordingSendEnabled_) {
    Serial.println("[REC] Cannot start: no available destination");
    return;
  }

  if (recordingSaveEnabled_) {
    if (!selectNextRecordingPath()) {
      Serial.println("[REC/SD] Cannot start: no available WAV filename");
      recordingSaveEnabled_ = false;
    } else {
      audioBufferCapacity_ = AUDIO_BUFFER_BYTES;
      audioBuffer_ = static_cast<uint8_t *>(ps_malloc(audioBufferCapacity_));
      if (audioBuffer_ == nullptr) {
        audioBufferCapacity_ = 0;
        recordingSaveEnabled_ = false;
        Serial.println("[REC/SD] Disabled for this recording: PSRAM allocation failed");
      }
    }
  }

  if (!recordingSaveEnabled_ && !recordingSendEnabled_) {
    Serial.println("[REC] Cannot start: all destinations failed to initialize");
    return;
  }

  if (!microphone_.available() || !microphone_.startCapture()) {
    free(audioBuffer_);
    audioBuffer_ = nullptr;
    audioBufferCapacity_ = 0;
    recordingSaveEnabled_ = false;
    recordingSendEnabled_ = false;
    Serial.println("[REC] Cannot start: microphone unavailable");
    return;
  }

  voiceRecording_ = true;
  recordingStartedMs_ = millis();
  audioBytesWritten_ = 0;
  audioStreamId_ = nextAudioStreamId_++;
  audioChunkIndex_ = 0;
  lastLedToggleMs_ = millis();
  writeStatusLed(true, true);
  Serial.printf("[REC] Recording started: %lu Hz, 16-bit, mono, max %.1f s\n",
                static_cast<unsigned long>(AppConfig::AUDIO_SAMPLE_RATE_HZ),
                AppConfig::AUDIO_MAX_RECORDING_MS / 1000.0f);
  if (recordingSendEnabled_) {
    Serial.printf("[REC/TCP] Streaming started: stream=%lu, device=%s:%u\n",
                  static_cast<unsigned long>(audioStreamId_),
                  WiFi.localIP().toString().c_str(), AppConfig::DATA_PORT);
  }
  if (recordingSaveEnabled_) {
    Serial.printf("[REC/SD] Buffering for %s (%u-byte PSRAM buffer)\n",
                  audioFilePath_, static_cast<unsigned>(audioBufferCapacity_));
  }
}

void SensorManager::stopVoiceRecording(const char *reason) {
  if (!voiceRecording_) return;

  const uint32_t durationMs = millis() - recordingStartedMs_;
  const bool shouldSave = recordingSaveEnabled_;
  Serial.printf("[REC] Recording ended: %s\n", reason);
  voiceRecording_ = false;
  microphone_.stopCapture();

  if (recordingSendEnabled_) sendAudioEnd();

  bool saved = false;
  if (shouldSave) {
    savingAudio_ = true;
    lastLedToggleMs_ = millis();
    writeStatusLed(true, true);
    Serial.printf("[REC/SD] Saving started: %s (%lu PCM bytes)\n",
                  audioFilePath_,
                  static_cast<unsigned long>(audioBytesWritten_));

    File audioFile = SD.open(audioFilePath_, FILE_WRITE);
    if (!audioFile) {
      Serial.println("[REC/SD] Failed to create the WAV file on microSD");
    } else {
      saved = writeWavHeader(audioFile, audioBytesWritten_);
      size_t offset = 0;
      while (saved && offset < audioBytesWritten_) {
        const size_t remaining = audioBytesWritten_ - offset;
        const size_t chunk = remaining > 4096 ? 4096 : remaining;
        const size_t written = audioFile.write(audioBuffer_ + offset, chunk);
        if (written != chunk) {
          saved = false;
          break;
        }
        offset += written;
        hapticMotor_.update();
        updateStatusLed();
      }
      audioFile.close();
    }
  }

  free(audioBuffer_);
  audioBuffer_ = nullptr;
  audioBufferCapacity_ = 0;
  savingAudio_ = false;
  recordingSendEnabled_ = false;
  recordingSaveEnabled_ = false;
  updateStatusLed(true);

  if (saved) {
    Serial.printf("[REC/SD] Save complete: %s\n", audioFilePath_);
  } else if (shouldSave) {
    Serial.printf("[REC/SD] Save failed or file may be incomplete: %s\n",
                  audioFilePath_);
  }
  Serial.printf("[REC] Complete: duration=%.2f s, PCM=%lu bytes\n",
                durationMs / 1000.0f,
                static_cast<unsigned long>(audioBytesWritten_));
  Serial.println("[REC] Ready for the next BOOT press");
}

void SensorManager::pollVoiceRecording() {
  size_t requestedSamples = AUDIO_CHUNK_SAMPLES;
  if (recordingSaveEnabled_) {
    const size_t remainingBytes = audioBufferCapacity_ - audioBytesWritten_;
    const size_t remainingSamples = remainingBytes / sizeof(int16_t);
    if (remainingSamples < requestedSamples) requestedSamples = remainingSamples;
  }
  if (requestedSamples == 0) {
    stopVoiceRecording("maximum duration reached");
    return;
  }

  int16_t samples[AUDIO_CHUNK_SAMPLES];
  const size_t samplesRead = microphone_.readPcm16(samples, requestedSamples);
  if (samplesRead == 0) {
    stopVoiceRecording("microphone read failed");
    return;
  }

  const size_t bytesRead = samplesRead * sizeof(int16_t);
  if (recordingSaveEnabled_) {
    memcpy(audioBuffer_ + audioBytesWritten_, samples, bytesRead);
    audioBytesWritten_ += static_cast<uint32_t>(bytesRead);
  } else {
    audioBytesWritten_ += static_cast<uint32_t>(bytesRead);
  }

  if (recordingSendEnabled_ && !sendAudioChunk(samples, samplesRead)) {
    recordingSendEnabled_ = false;
    Serial.println("[REC/TCP] Streaming interrupted: laptop connection lost");
    if (!recordingSaveEnabled_) {
      stopVoiceRecording("no destination remains");
      return;
    }
  }

  if ((recordingSaveEnabled_ && audioBytesWritten_ >= audioBufferCapacity_) ||
      millis() - recordingStartedMs_ >= AppConfig::AUDIO_MAX_RECORDING_MS) {
    stopVoiceRecording("maximum duration reached");
  }
}

bool SensorManager::sendAudioChunk(const int16_t *samples,
                                   size_t sampleCount) {
  AudioPacket packet = {};
  packet.prefix.streamId = audioStreamId_;
  packet.prefix.chunkIndex = audioChunkIndex_;
  packet.prefix.sampleRateHz = AppConfig::AUDIO_SAMPLE_RATE_HZ;
  packet.prefix.sampleCount = static_cast<uint16_t>(sampleCount);
  packet.prefix.channels = 1;
  packet.prefix.bitsPerSample = 16;
  memcpy(packet.samples, samples, sampleCount * sizeof(int16_t));

  uint16_t flags = WireProtocol::FLAG_MORE_FRAGMENTS;
  if (audioChunkIndex_ == 0) flags |= WireProtocol::FLAG_STREAM_START;
  const size_t payloadBytes =
      sizeof(packet.prefix) + sampleCount * sizeof(int16_t);
  if (!transport_.sendFrame(WireProtocol::MessageType::AudioPcm, &packet,
                            payloadBytes, flags)) {
    return false;
  }
  ++audioChunkIndex_;
  return true;
}

void SensorManager::sendAudioEnd() {
  WireProtocol::AudioPcmPrefix prefix = {};
  prefix.streamId = audioStreamId_;
  prefix.chunkIndex = audioChunkIndex_;
  prefix.sampleRateHz = AppConfig::AUDIO_SAMPLE_RATE_HZ;
  prefix.channels = 1;
  prefix.bitsPerSample = 16;
  if (transport_.sendFrame(WireProtocol::MessageType::AudioPcm, &prefix,
                           sizeof(prefix), WireProtocol::FLAG_STREAM_END)) {
    Serial.printf("[REC/TCP] Streaming complete: stream=%lu, chunks=%lu\n",
                  static_cast<unsigned long>(audioStreamId_),
                  static_cast<unsigned long>(audioChunkIndex_));
  } else {
    Serial.printf("[REC/TCP] End marker failed: stream=%lu\n",
                  static_cast<unsigned long>(audioStreamId_));
  }
}

bool SensorManager::selectNextRecordingPath() {
  audioFilePath_[0] = '\0';
  for (uint16_t index = 1; index <= 9999; ++index) {
    snprintf(audioFilePath_, sizeof(audioFilePath_), "/recording_%04u.wav",
             index);
    if (SD.exists(audioFilePath_)) continue;
    return true;
  }
  return false;
}

void SensorManager::writeStatusLed(bool on, bool force) {
  if (!ledUsable_) return;
  if (!force && ledOutputOn_ == on) return;
  ledOutputOn_ = on;
  const uint8_t level = (on == AppConfig::RECORDING_LED_ACTIVE_LOW) ? LOW : HIGH;
  digitalWrite(AppConfig::RECORDING_LED_PIN, level);
}

void SensorManager::updateStatusLed(bool force) {
  if (!ledUsable_) return;
  const bool busy = voiceRecording_ || savingAudio_;
  if (!busy) {
    writeStatusLed(WiFi.status() == WL_CONNECTED, force);
    return;
  }

  const uint32_t now = millis();
  if (force || now - lastLedToggleMs_ >= AppConfig::LED_FAST_BLINK_MS) {
    lastLedToggleMs_ = now;
    writeStatusLed(!ledOutputOn_, true);
  }
}

void SensorManager::sendStatus() {
  WireProtocol::StatusPayload status = {};
  status.uptimeMs = millis();
  status.freeHeapBytes = ESP.getFreeHeap();
  status.wifiRssiDbm = static_cast<int8_t>(WiFi.RSSI());
  status.sensorFlags = (imu_.available() ? 1u : 0u) |
                       (ultrasonic_.available() ? 2u : 0u) |
                       (microphone_.available() ? 4u : 0u) |
                       (hapticMotor_.ready() ? 8u : 0u);
  transport_.sendFrame(WireProtocol::MessageType::Status, &status, sizeof(status));
  lastStatusMs_ = millis();
}
