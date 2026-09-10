#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>

class MicrophoneSensor {
 public:
  bool begin();
  bool startCapture();
  void stopCapture();
  size_t readPcm16(int16_t *samples, size_t capacitySamples);
  bool available() const { return available_; }
  bool capturing() const { return capturing_; }

  static constexpr uint8_t PDM_DATA_PIN = 41;
  static constexpr uint8_t PDM_CLOCK_PIN = 42;

 private:
  I2SClass i2s_;
  bool available_ = false;
  bool capturing_ = false;
};
