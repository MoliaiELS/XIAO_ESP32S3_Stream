#include "MicrophoneSensor.h"

#include "Config.h"

bool MicrophoneSensor::begin() {
  // Probe the I2S/PDM peripheral once, then leave it stopped until recording.
  if (!startCapture()) return false;
  stopCapture();
  Serial.println("PDM microphone initialized");
  return true;
}

bool MicrophoneSensor::startCapture() {
  if (capturing_) return true;

  i2s_.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
  if (!i2s_.begin(I2S_MODE_PDM_RX, AppConfig::AUDIO_SAMPLE_RATE_HZ,
                  I2S_DATA_BIT_WIDTH_16BIT,
                  I2S_SLOT_MODE_MONO)) {
    available_ = false;
    Serial.println("Failed to initialize PDM microphone");
    return false;
  }

  available_ = true;
  capturing_ = true;
  return true;
}

void MicrophoneSensor::stopCapture() {
  if (!capturing_) return;
  i2s_.end();
  capturing_ = false;
}

size_t MicrophoneSensor::readPcm16(int16_t *samples, size_t capacitySamples) {
  if (!capturing_ || samples == nullptr || capacitySamples == 0) return 0;
  const size_t bytesRequested = capacitySamples * sizeof(int16_t);
  const size_t bytesRead =
      i2s_.readBytes(reinterpret_cast<char *>(samples), bytesRequested);
  return bytesRead / sizeof(int16_t);
}
