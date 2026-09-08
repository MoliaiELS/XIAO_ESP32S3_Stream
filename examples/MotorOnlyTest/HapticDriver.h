#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "HardwareConfig.h"

// Minimal DRV2605L RTP/open-loop driver. No external Arduino libraries.
// Signed RTP: 0 = off, 127 = positive full scale. No ROM/calibration/OTP writes.
class HapticDriver {
 public:
  bool present = false, configured = false, fault = false, active = false;
  bool statusRead = false;
  uint8_t chipStatus = 0;
  const char* firstError = "none";
  uint8_t errorReg = 0, errorCode = 0, errorStatus = 0;
  uint32_t errorAt = 0;
  bool stopConfirmed = true;
  void printFault() const {
    Serial.printf("FIRST_ERROR=%s ms=%lu reg=0x%02X code=%u captured_status=0x%02X stop_confirmed=%d\n",
      firstError, (unsigned long)errorAt, errorReg, errorCode, errorStatus, stopConfirmed);
  }
  bool begin() {
    firstError = "none"; errorAt = 0; errorReg = errorCode = errorStatus = 0;
    configured = false; fault = false; active = false;
    uint8_t status = 0;
    statusRead = read(0x00, status);
    chipStatus = status;
    present = statusRead && ((status >> 5) == 7 || (status >> 5) == 3);
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
    if (!checkStatus()) { fail(); return false; }
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
    stopConfirmed = zero && standby;
    return stopConfirmed;
  }
  void update(uint32_t now) {
    if (!active) return;
    if (uint32_t(now - started_) >= duration_) { stop(); return; }
    if (uint32_t(now - checked_) >= 20) {
      checked_ = now;
      if (!checkStatus()) fail();
    }
  }
 private:
  uint32_t started_ = 0, duration_ = 0, checked_ = 0;
  void record(const char* reason, uint8_t reg, uint8_t code, uint8_t status = 0) {
    if (strcmp(firstError, "none")) return;
    firstError = reason; errorReg = reg; errorCode = code;
    errorStatus = status; errorAt = millis();
  }
  bool checkStatus() {
    uint8_t status;
    if (!read(0x00, status)) return false;
    if (status & 0x03) {
      record("CHIP_PROTECTION", 0x00, status & 0x03, status);
      return false;
    }
    return true;
  }
  bool write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(0x5A); Wire.write(reg); Wire.write(value);
    uint8_t code = Wire.endTransmission();
    if (code) record("I2C_WRITE", reg, code);
    return code == 0;
  }
  bool read(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(0x5A); Wire.write(reg);
    uint8_t code = Wire.endTransmission(false);
    if (code) { record("I2C_READ_POINTER", reg, code); return false; }
    size_t count = Wire.requestFrom(uint8_t(0x5A), uint8_t(1));
    if (count != 1) { record("I2C_SHORT_READ", reg, uint8_t(count)); return false; }
    value = Wire.read(); return true;
  }
  void fail() { stop(); configured = false; fault = true; }
};
