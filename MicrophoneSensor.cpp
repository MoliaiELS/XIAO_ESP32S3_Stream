#include "MicrophoneSensor.h"

bool MicrophoneSensor::begin() {
  // Reserved for Arduino-ESP32 3.0.x ESP_I2S.h / I2SClass:
  // I2S.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
  // I2S.begin(I2S_MODE_PDM_RX, SAMPLE_RATE_HZ,
  //           I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  available_ = false;
  return available_;
}

size_t MicrophoneSensor::readPcm16(int16_t *samples, size_t capacitySamples) {
  (void)samples;
  (void)capacitySamples;
  // TODO: read a bounded PCM chunk without blocking the command receive loop.
  return 0;
}
