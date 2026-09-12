#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

// Minimal DRV2605L driver for one LRA on one I2C bus. It uses RTP open-loop
// mode; motorUpdate() must be called frequently so timed pulses stop safely.
class HapticMotor {
 public:
  explicit HapticMotor(TwoWire &wire = Wire) : wire_(wire) {}

  bool begin(int sda, int scl, float peakVolts, float frequencyHz) {
    if (active_) {
      Serial.println("[HAPTIC] Init rejected: stop motor first");
      return false;
    }
    ready_ = false;
    fault_ = false;
    initialized_ = false;
    stopConfirmed_ = false;
    firstError_ = nullptr;
    errorAt_ = 0;
    errorReg_ = errorCode_ = errorStatus_ = 0;

    Serial.printf("[HAPTIC] Init DRV2605L: SDA=GPIO%d SCL=GPIO%d address=0x5A\n",
                  sda, scl);
    if (sda < 0 || scl < 0 || sda == scl ||
        !wire_.begin(sda, scl, 100000)) {
      record("BUS_INIT_OR_PIN_CONFLICT", 0, 0);
      return initFailed();
    }
    wire_.setTimeOut(20);
    initialized_ = true;
    if (!stop()) return false;

    if (!isfinite(peakVolts) || peakVolts < 0.1f || peakVolts > 3.3f ||
        !isfinite(frequencyHz) || frequencyHz < 125.0f ||
        frequencyHz > 300.0f) {
      record("INVALID_LRA_PARAMETERS", 0, 0);
      return initFailed();
    }

    uint8_t status = 0;
    if (!readReg(0x00, status)) return initFailed();
    Serial.printf("[HAPTIC] STATUS=0x%02X device_id=%u\n", status, status >> 5);
    if ((status >> 5) != 7) {
      record("UNSUPPORTED_CHIP_ID", 0, status >> 5, status);
      return initFailed();
    }
    if (status & 0x03) {
      record("CHIP_PROTECTION", 0, status & 0x03, status);
      return initFailed();
    }

    const uint8_t clamp = uint8_t(peakVolts / 0.02122f);
    const uint8_t period =
        uint8_t(1000000.0f / (98.46f * frequencyHz) + 0.5f);
    const uint8_t registers[] = {0x17, 0x1A, 0x1D, 0x20};
    const uint8_t values[] = {clamp, 0xB6, 0x81, period};
    for (size_t index = 0; index < 4; ++index) {
      if (!writeReg(registers[index], values[index])) return fail();
      uint8_t actual = 0;
      if (!readReg(registers[index], actual)) return fail();
      if (actual != values[index]) {
        record("CONFIG_READBACK", registers[index], actual);
        return fail();
      }
    }

    ready_ = true;
    Serial.printf("[HAPTIC] Ready: LRA peak<=%.2fV requested=%.1fHz actual=%.1fHz\n",
                  peakVolts, frequencyHz,
                  1000000.0f / (98.46f * period));
    return true;
  }

  bool beep(uint8_t preset) {
    if (preset < 1 || preset > 3) {
      Serial.println("[HAPTIC] Invalid preset; use 1..3");
      return false;
    }
    return pulse(preset == 1 ? 32 : preset == 2 ? 64 : 100,
                 preset == 1 ? 150 : 300);
  }

  bool pulse(uint8_t strength, uint32_t durationMs) {
    if (!ready_ || fault_) {
      Serial.println("[HAPTIC] Pulse rejected: driver is not ready");
      debug();
      return false;
    }
    if (!strength || strength > 127 || !durationMs || durationMs > 1000) {
      Serial.println("[HAPTIC] Pulse requires strength=1..127, duration=1..1000ms");
      return false;
    }
    if (!checkStatus() || !writeReg(0x02, 0) || !writeReg(0x01, 0x05)) {
      return fail();
    }
    delayMicroseconds(1000);
    if (!writeReg(0x02, strength)) return fail();
    startedMs_ = checkedMs_ = millis();
    durationMs_ = durationMs;
    active_ = true;
    Serial.printf("[HAPTIC] Pulse started: strength=%u/127 duration=%lu ms\n",
                  strength, static_cast<unsigned long>(durationMs));
    return true;
  }

  void update() {
    if (!active_) return;
    const uint32_t now = millis();
    if (uint32_t(now - startedMs_) >= durationMs_) {
      stop();
    } else if (uint32_t(now - checkedMs_) >= 20) {
      checkedMs_ = now;
      if (!checkStatus()) fail();
    }
  }

  bool stop() {
    if (!initialized_) {
      Serial.println("[HAPTIC] Stop rejected: driver is not initialized");
      return false;
    }
    const bool zero = writeReg(0x02, 0);
    const bool standby = writeReg(0x01, 0x40);
    active_ = false;
    stopConfirmed_ = zero && standby;
    if (!stopConfirmed_) {
      ready_ = false;
      fault_ = true;
      Serial.println("[HAPTIC] Stop failed: disconnect motor power and inspect I2C");
    }
    return stopConfirmed_;
  }

  bool ready() const { return ready_ && !fault_; }
  bool active() const { return active_; }
  bool fault() const { return fault_; }

  void debug() const {
    Serial.printf("[HAPTIC] ready=%d active=%d fault=%d stop_confirmed=%d first_error=%s ms=%lu reg=0x%02X code=%u status=0x%02X\n",
                  ready(), active_, fault_, stopConfirmed_,
                  firstError_ ? firstError_ : "none",
                  static_cast<unsigned long>(errorAt_), errorReg_, errorCode_,
                  errorStatus_);
  }

 private:
  TwoWire &wire_;
  bool initialized_ = false, ready_ = false, active_ = false, fault_ = false;
  bool stopConfirmed_ = false;
  uint32_t startedMs_ = 0, checkedMs_ = 0, durationMs_ = 0, errorAt_ = 0;
  const char *firstError_ = nullptr;
  uint8_t errorReg_ = 0, errorCode_ = 0, errorStatus_ = 0;

  void record(const char *reason, uint8_t reg, uint8_t code,
              uint8_t status = 0) {
    if (firstError_) return;
    firstError_ = reason;
    errorAt_ = millis();
    errorReg_ = reg;
    errorCode_ = code;
    errorStatus_ = status;
  }

  bool writeReg(uint8_t reg, uint8_t value) {
    wire_.beginTransmission(0x5A);
    wire_.write(reg);
    wire_.write(value);
    const uint8_t code = wire_.endTransmission();
    if (code) record("I2C_WRITE", reg, code);
    return code == 0;
  }

  bool readReg(uint8_t reg, uint8_t &value) {
    wire_.beginTransmission(0x5A);
    wire_.write(reg);
    const uint8_t code = wire_.endTransmission(false);
    if (code) {
      record("I2C_READ_POINTER", reg, code);
      return false;
    }
    const size_t count = wire_.requestFrom(uint8_t(0x5A), uint8_t(1));
    if (count != 1) {
      record("I2C_SHORT_READ", reg, uint8_t(count));
      return false;
    }
    value = wire_.read();
    return true;
  }

  bool checkStatus() {
    uint8_t status = 0;
    if (!readReg(0x00, status)) return false;
    if (status & 0x03) {
      record("CHIP_PROTECTION", 0, status & 0x03, status);
      return false;
    }
    return true;
  }

  bool initFailed() {
    fault_ = true;
    debug();
    return false;
  }

  bool fail() {
    const bool zero = writeReg(0x02, 0);
    const bool standby = writeReg(0x01, 0x40);
    active_ = false;
    stopConfirmed_ = zero && standby;
    ready_ = false;
    fault_ = true;
    debug();
    return false;
  }
};
