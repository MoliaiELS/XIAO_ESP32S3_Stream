#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "HardwareConfig.h"

// Minimal DRV2605L RTP/open-loop driver. No external Arduino libraries.
// Signed RTP: 0 = off, 127 = positive full scale. No ROM/calibration/OTP writes.
class HapticDriver {
 public:
  bool present = false, configured = false, fault = false, active = false;
  bool begin() {
    configured = false; fault = false; active = false;
    uint8_t status = 0;
    present = read(0x00, status) && ((status >> 5) == 7 || (status >> 5) == 3);
    if (!present) return false;
    if (!stop()) return false;
    using namespace HardwareConfig;
    if (MOTOR_TYPE == MotorType::Unknown || !isfinite(MOTOR_VOLTAGE_LIMIT) ||
        MOTOR_VOLTAGE_LIMIT <= 0 || MOTOR_VOLTAGE_LIMIT > 3.3f) return false;
    const bool lra = MOTOR_TYPE == MotorType::LRA;
    if (lra && (!isfinite(LRA_FREQUENCY_HZ) || LRA_FREQUENCY_HZ < 100 ||
                LRA_FREQUENCY_HZ > 300)) return false;
    // TI datasheet Eq.6 ERM average / Eq.9 LRA peak; round down.
    const uint8_t clamp = uint8_t(MOTOR_VOLTAGE_LIMIT / (lra ? 0.02122f : 0.02159f));
    if (clamp == 0) return false;
    bool ok = write(0x17, clamp) && write(0x1A, lra ? 0xB6 : 0x36) &&
              write(0x1D, lra ? 0x81 : 0xA0);
    if (ok && lra) ok = write(0x20, uint8_t(1000000.0f / (98.46f * LRA_FREQUENCY_HZ) + 0.5f));
    configured = ok;
    if (!ok) fail();
    return ok;
  }
  bool pulse(uint8_t amplitude, uint32_t duration, uint32_t now) {
    if (!configured || fault || amplitude == 0 || amplitude > 127 ||
        duration == 0 || duration > HardwareConfig::MAX_PULSE_MS) return false;
    uint8_t status;
    if (!read(0x00, status) || (status & 0x03)) { fail(); return false; }
    if (!write(0x02, 0) || !write(0x01, 0x05)) { fail(); return false; }
    delayMicroseconds(1000);  // standby wake, not the vibration duration
    if (!write(0x02, amplitude)) { fail(); return false; }
    active = true; started_ = now; duration_ = duration; checked_ = now;
    return true;
  }
  bool stop() {
    bool zero = write(0x02, 0);
    bool standby = write(0x01, 0x40);
    active = false;
    if (!zero || !standby) { fault = true; configured = false; }
    return zero && standby;
  }
  void update(uint32_t now) {
    if (!active) return;
    if (uint32_t(now - started_) >= duration_) { stop(); return; }
    if (uint32_t(now - checked_) >= 20) {
      checked_ = now;
      uint8_t status;
      if (!read(0x00, status) || (status & 0x03)) fail();
    }
  }
 private:
  uint32_t started_ = 0, duration_ = 0, checked_ = 0;
  bool write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(0x5A); Wire.write(reg); Wire.write(value);
    return Wire.endTransmission() == 0;
  }
  bool read(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(0x5A); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(uint8_t(0x5A), uint8_t(1)) != 1) return false;
    value = Wire.read(); return true;
  }
  void fail() { stop(); configured = false; fault = true; }
};
