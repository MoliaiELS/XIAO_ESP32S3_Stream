#pragma once

#include <Arduino.h>

class MicrophoneSensor {
 public:
  bool begin();
  size_t readPcm16(int16_t *samples, size_t capacitySamples);
  bool available() const { return available_; }

  static constexpr uint8_t PDM_DATA_PIN = 41;
  static constexpr uint8_t PDM_CLOCK_PIN = 42;
  static constexpr uint32_t SAMPLE_RATE_HZ = 16000;

 private:
  bool available_ = false;
};
