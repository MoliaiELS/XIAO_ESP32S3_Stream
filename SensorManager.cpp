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
     AppConfig::AUDIO_SD_MAX_RECORDING_MS) /
    1000;

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

void setRecordingLed(bool recording) {
  const uint8_t level =
      (recording == AppConfig::RECORDING_LED_ACTIVE_LOW) ? LOW : HIGH;
  digitalWrite(AppConfig::RECORDING_LED_PIN, level);
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

  pinMode(AppConfig::RECORDING_LED_PIN, OUTPUT);
  setRecordingLed(false);
  Serial.printf("Recording LED ready on GPIO%u (active low)\n",
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
    } else {
      Serial.println("microSD mount failed on both known CS pins");
      Serial.println("Check card insertion, FAT32 format, B2B connector and J3");
    }
    Serial.println("Press BOOT to start/stop a WAV recording");
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
  // Local recording is independent of Wi-Fi and the laptop TCP connection.
  if (AppConfig::ENABLE_SD_AUDIO_RECORDING) {
    if (buttonPressed()) {
      if (voiceRecording_) {
        stopVoiceRecording("button pressed");
      } else {
        startVoiceRecording();
      }
    }
    if (voiceRecording_) pollVoiceRecording();
  }

  if (!transport_.connected()) return;

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
  if (!sdReady_) {
    Serial.println("Cannot record: microSD card is unavailable");
    return;
  }
  if (!selectNextRecordingPath()) {
    Serial.println("Cannot record: no available WAV filename");
    return;
  }

  audioBufferCapacity_ = AUDIO_BUFFER_BYTES;
  audioBuffer_ = static_cast<uint8_t *>(ps_malloc(audioBufferCapacity_));
  if (audioBuffer_ == nullptr) {
    audioBufferCapacity_ = 0;
    Serial.println("Cannot record: PSRAM allocation failed");
    return;
  }

  if (!microphone_.available() || !microphone_.startCapture()) {
    free(audioBuffer_);
    audioBuffer_ = nullptr;
    audioBufferCapacity_ = 0;
    Serial.println("Cannot record: microphone unavailable");
    return;
  }

  voiceRecording_ = true;
  recordingStartedMs_ = millis();
  audioBytesWritten_ = 0;
  setRecordingLed(true);
  Serial.println("[REC] Recording started");
  Serial.printf("[REC] File: %s, 16000 Hz, 16-bit, mono\n", audioFilePath_);
  Serial.printf("[REC] PSRAM buffer: %u bytes\n",
                static_cast<unsigned>(audioBufferCapacity_));
}

void SensorManager::stopVoiceRecording(const char *reason) {
  if (!voiceRecording_) return;

  const uint32_t durationMs = millis() - recordingStartedMs_;
  Serial.printf("[REC] Recording ended: %s\n", reason);
  setRecordingLed(false);
  Serial.println("[REC] Finalizing WAV file...");

  microphone_.stopCapture();
  bool saved = false;
  File audioFile = SD.open(audioFilePath_, FILE_WRITE);
  if (!audioFile) {
    Serial.println("[REC] Failed to create the WAV file on microSD");
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
    }
    audioFile.close();
  }

  free(audioBuffer_);
  audioBuffer_ = nullptr;
  audioBufferCapacity_ = 0;
  voiceRecording_ = false;

  if (saved) {
    Serial.printf("[REC] Save complete: %s\n", audioFilePath_);
    Serial.printf("[REC] Duration: %.2f s, PCM data: %lu bytes\n",
                  durationMs / 1000.0f,
                  static_cast<unsigned long>(audioBytesWritten_));
  } else {
    Serial.printf("[REC] Save failed or file may be incomplete: %s\n",
                  audioFilePath_);
  }
}

void SensorManager::pollVoiceRecording() {
  const size_t remainingBytes = audioBufferCapacity_ - audioBytesWritten_;
  const size_t remainingSamples = remainingBytes / sizeof(int16_t);
  const size_t requestedSamples =
      remainingSamples > AUDIO_CHUNK_SAMPLES ? AUDIO_CHUNK_SAMPLES
                                             : remainingSamples;
  if (requestedSamples == 0) {
    stopVoiceRecording("maximum duration reached");
    return;
  }

  int16_t *destination = reinterpret_cast<int16_t *>(
      audioBuffer_ + audioBytesWritten_);
  const size_t samplesRead =
      microphone_.readPcm16(destination, requestedSamples);
  if (samplesRead == 0) {
    stopVoiceRecording("microphone read failed");
    return;
  }

  const size_t bytesRead = samplesRead * sizeof(int16_t);
  audioBytesWritten_ += static_cast<uint32_t>(bytesRead);

  if (audioBytesWritten_ >= audioBufferCapacity_ ||
      millis() - recordingStartedMs_ >= AppConfig::AUDIO_SD_MAX_RECORDING_MS) {
    stopVoiceRecording("maximum duration reached");
  }
}

bool SensorManager::selectNextRecordingPath() {
  for (uint16_t index = 1; index <= 9999; ++index) {
    snprintf(audioFilePath_, sizeof(audioFilePath_), "/recording_%04u.wav",
             index);
    if (SD.exists(audioFilePath_)) continue;
    return true;
  }
  return false;
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
