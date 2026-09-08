#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// One MotorBus per physical I2C controller. Use only from one task/loop.
// Direct: one DRV2605L per bus. With TCA9548A: one per mux channel.
class MotorBus {
 public:
  explicit MotorBus(TwoWire& wire, uint8_t muxAddress = 0)
      : wire_(wire), mux_(muxAddress) {}
  MotorBus(const MotorBus&) = delete;
  MotorBus& operator=(const MotorBus&) = delete;

 private:
  friend class BspMotor;
  TwoWire& wire_;
  uint8_t mux_;
  bool begun_ = false;
  int sda_ = -1, scl_ = -1;
  const void* owners_[8] = {};
  bool begin(int sda, int scl) {
    if (sda < 0 || scl < 0 || sda == scl ||
        (mux_ && (mux_ < 0x70 || mux_ > 0x77))) return false;
    if (begun_) return sda == sda_ && scl == scl_;
    if (!wire_.begin(sda, scl, 100000)) return false;
    wire_.setTimeOut(20);
    sda_ = sda; scl_ = scl; begun_ = true;
    return true;
  }
  bool claim(uint8_t channel, const void* owner) {
    if (channel > 7 || (!mux_ && channel != 0)) return false;
    if (owners_[channel] && owners_[channel] != owner) return false;
    owners_[channel] = owner;
    return true;
  }
  uint8_t select(uint8_t channel) {
    if (!mux_) return 0;
    wire_.beginTransmission(mux_);
    wire_.write(uint8_t(1u << channel)); // ONLY one channel active: all motors use 0x5A
    return wire_.endTransmission();
  }
};

class BspMotor {
 public:
  // name and bus must outlive this object. For a mux, specify channel 0..7.
  BspMotor(MotorBus& bus, const char* name, uint8_t channel = 0)
      : bus_(bus), name_(name), channel_(channel) {}
  BspMotor(const BspMotor&) = delete;
  BspMotor& operator=(const BspMotor&) = delete;

  // Example: motor_init(D4, D5): XIAO D4/GPIO5 -> SDA, D5/GPIO6 -> SCL.
  // Parameters are GPIO numbers / Arduino Dn constants, not physical pin indices.
  // LRA1040 initial configuration: 2.5 V PEAK ceiling (not Vrms), 200 Hz.
  bool motor_init(int sda, int scl, float peakVolts = 2.5f, float frequencyHz = 200.0f) {
    if (active_) { Serial.printf("[%s] INIT rejected: stop motor first\n", name_); return false; }
    ready_ = false; fault_ = false; error_ = nullptr; errorAt_ = 0;
    errorReg_ = errorCode_ = errorStatus_ = 0;
    Serial.printf("[%s] INIT SDA=GPIO%d SCL=GPIO%d address=0x5A mux=0x%02X channel=%u\n",
                  name_, sda, scl, bus_.mux_, channel_);
    if (!bus_.begin(sda, scl)) {
      record("BUS_INIT_OR_PIN_CONFLICT", 0, 0); return initFailed();
    }
    if (!bus_.claim(channel_, this)) {
      record("DUPLICATE_OR_INVALID_CHANNEL", 0, channel_); return initFailed();
    }
    claimed_ = true;
    if (!motor_stop()) return false;
    if (!isfinite(peakVolts) || peakVolts < 0.1f || peakVolts > 3.3f ||
        !isfinite(frequencyHz) || frequencyHz < 125 || frequencyHz > 300) {
      record("INVALID_LRA_PARAMETERS", 0, 0); return initFailed();
    }
    uint8_t status;
    if (!readReg(0x00, status)) return initFailed();
    Serial.printf("[%s] STATUS=0x%02X ID=%u\n", name_, status, status >> 5);
    if ((status >> 5) != 7) { // This implementation targets DRV2605L.
      record("UNSUPPORTED_CHIP_ID", 0, status >> 5, status); return initFailed();
    }
    if (status & 3) {
      record("CHIP_PROTECTION", 0, status & 3, status); return initFailed();
    }
    const uint8_t clamp = uint8_t(peakVolts / 0.02122f);
    const uint8_t period = uint8_t(1000000.0f / (98.46f * frequencyHz) + 0.5f);
    // TI: LRA mode, open loop, signed RTP. Zero is OFF, 127 is full positive drive.
    const uint8_t regs[] = {0x17, 0x1A, 0x1D, 0x20};
    const uint8_t values[] = {clamp, 0xB6, 0x81, period};
    for (unsigned i = 0; i < 4; ++i) {
      if (!writeReg(regs[i], values[i])) { fail(); return false; }
      uint8_t actual;
      if (!readReg(regs[i], actual)) { fail(); return false; }
      if (actual != values[i]) {
        record("CONFIG_READBACK", regs[i], actual); fail(); return false;
      }
    }
    ready_ = true;
    Serial.printf("[%s] READY LRA peak<=%.2fV requested=%.1fHz actual=%.1fHz; motor stopped\n",
                  name_, peakVolts, frequencyHz, 1000000.0f / (98.46f * period));
    return true;
  }

  // Presets matching serial 1/2/3. Non-blocking: call motor_update() in loop.
  bool motor_beep(uint8_t preset) {
    if (preset < 1 || preset > 3) {
      Serial.printf("[%s] BEEP invalid preset (use 1..3)\n", name_); return false;
    }
    return motor_beep(preset == 1 ? 32 : preset == 2 ? 64 : 100, preset == 1 ? 150 : 300);
  }
  bool motor_beep(uint8_t strength, uint32_t durationMs) {
    if (!ready_ || fault_) { Serial.printf("[%s] BEEP rejected: not ready/fault\n", name_); motor_debug(); return false; }
    if (!strength || strength > 127 || !durationMs || durationMs > 1000) {
      Serial.printf("[%s] BEEP requires strength=1..127 duration=1..1000ms\n", name_); return false;
    }
    if (!checkStatus() || !writeReg(0x02, 0) || !writeReg(0x01, 0x05)) { fail(); return false; }
    delayMicroseconds(1000);
    if (!writeReg(0x02, strength)) { fail(); return false; }
    started_ = checked_ = millis(); duration_ = durationMs; active_ = true;
    Serial.printf("[%s] BEEP strength=%u/127 duration=%lu ms\n", name_, strength, (unsigned long)durationMs);
    return true; // another beep on THIS instance restarts its timer
  }
  void motor_update() {
    if (!active_) return;
    uint32_t now = millis();
    if (uint32_t(now - started_) >= duration_) { motor_stop(); return; }
    if (uint32_t(now - checked_) >= 20) {
      checked_ = now;
      if (!checkStatus()) fail();
    }
  }
  bool motor_stop() {
    if (!claimed_) { Serial.printf("[%s] STOP rejected: motor not initialized\n", name_); return false; }
    bool zero = writeReg(0x02, 0);
    bool standby = writeReg(0x01, 0x40);
    active_ = false; stopConfirmed_ = zero && standby;
    if (!stopConfirmed_) { ready_ = false; fault_ = true; motor_debug(); }
    Serial.printf("[%s] STOP %s\n", name_, stopConfirmed_ ? "OK" : "FAILED: disconnect driver power");
    return stopConfirmed_;
  }
  bool motor_ready() const { return ready_ && !fault_; }
  bool motor_active() const { return active_; }
  bool motor_fault() const { return fault_; }
  // Prints stored evidence: no register reads, no clearing hardware status flags.
  void motor_debug() const {
    Serial.printf("[%s] ready=%d active=%d fault=%d stop_confirmed=%d FIRST_ERROR=%s ms=%lu reg=0x%02X code=%u status=0x%02X\n",
      name_, ready_, active_, fault_, stopConfirmed_, error_ ? error_ : "none",
      (unsigned long)errorAt_, errorReg_, errorCode_, errorStatus_);
  }

 private:
  MotorBus& bus_;
  const char* name_;
  uint8_t channel_;
  bool claimed_ = false, ready_ = false, active_ = false, fault_ = false, stopConfirmed_ = false;
  uint32_t started_ = 0, checked_ = 0, duration_ = 0, errorAt_ = 0;
  const char* error_ = nullptr;
  uint8_t errorReg_ = 0, errorCode_ = 0, errorStatus_ = 0;
  void record(const char* reason, uint8_t reg, uint8_t code, uint8_t status = 0) {
    if (error_) return;
    error_ = reason; errorAt_ = millis(); errorReg_ = reg; errorCode_ = code; errorStatus_ = status;
  }
  bool route() {
    if (!claimed_) { record("NOT_INITIALIZED", 0, 0); return false; }
    uint8_t code = bus_.select(channel_);
    if (code) record("MUX_SELECT", bus_.mux_, code);
    return code == 0;
  }
  bool writeReg(uint8_t reg, uint8_t value) {
    if (!route()) return false;
    bus_.wire_.beginTransmission(0x5A); bus_.wire_.write(reg); bus_.wire_.write(value);
    uint8_t code = bus_.wire_.endTransmission();
    if (code) record("I2C_WRITE", reg, code);
    return code == 0;
  }
  bool readReg(uint8_t reg, uint8_t& value) {
    if (!route()) return false;
    bus_.wire_.beginTransmission(0x5A); bus_.wire_.write(reg);
    uint8_t code = bus_.wire_.endTransmission(false);
    if (code) { record("I2C_READ_POINTER", reg, code); return false; }
    size_t count = bus_.wire_.requestFrom(uint8_t(0x5A), uint8_t(1));
    if (count != 1) { record("I2C_SHORT_READ", reg, uint8_t(count)); return false; }
    value = bus_.wire_.read(); return true;
  }
  bool checkStatus() {
    uint8_t status;
    if (!readReg(0x00, status)) return false;
    if (status & 3) { record("CHIP_PROTECTION", 0, status & 3, status); return false; }
    return true;
  }
  bool initFailed() { fault_ = true; motor_debug(); return false; }
  void fail() { motor_stop(); ready_ = false; fault_ = true; motor_debug(); }
};
