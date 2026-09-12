#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// DYP-A22 UART automatic protocol parser. Independent from Arduino for tests.
class UltrasonicParser {
 public:
  enum class Unit : uint8_t { Millimetres = 0, Microseconds = 1 };
  enum class Status : uint8_t {
    Waiting = 0,
    Distance = 1,
    NoTarget = 2,
    Interference = 3,
    Invalid = 4
  };

  explicit UltrasonicParser(Unit unit = Unit::Millimetres) : unit_(unit) {}
  uint32_t bytes = 0, frames = 0, checksumErrors = 0, partialTimeouts = 0;
  uint32_t sampleMs = 0, noTargetCount = 0, interferenceCount = 0;
  uint16_t raw = 0;
  Status status = Status::Waiting;

  bool fresh(uint32_t now, uint32_t timeout = 1500) const {
    return frames != 0 && uint32_t(now - sampleMs) <= timeout;
  }
  bool valid(uint32_t now, uint32_t timeout = 1500) const {
    return fresh(now, timeout) && status == Status::Distance;
  }
  float distanceMm(uint32_t now, uint32_t timeout = 1500) const {
    if (!valid(now, timeout)) return NAN;
    return unit_ == Unit::Millimetres ? float(raw) : float(raw) / 5.75f;
  }
  Unit unit() const { return unit_; }
  const char *statusText() const;
  void feed(uint8_t value, uint32_t now);

 private:
  Unit unit_;
  uint8_t buffer_[4] = {};
  size_t used_ = 0;
  uint32_t lastByteMs_ = 0;
};

#ifdef ARDUINO
#include <Arduino.h>
#include "TransportProtocol.h"

class UltrasonicSensor {
 public:
  bool begin();
  void poll();
  bool read(WireProtocol::UltrasonicPayload &sample);
  bool available() const { return available_; }
  void debug() const;

 private:
  HardwareSerial uart_{2};
  UltrasonicParser parser_;
  bool available_ = false;
  bool firstFrameReported_ = false;
  uint32_t emittedFrames_ = 0;
};
#endif
