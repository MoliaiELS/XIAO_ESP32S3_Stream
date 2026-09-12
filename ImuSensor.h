#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// UART protocol: 7E 23 total_length function payload sum8. Floats are LE.
// Kept independent from Arduino so the parser can be host-tested.
class ImuParser {
 public:
  struct Quaternion {
    float w = 0, x = 0, y = 0, z = 0;
  } quaternion;
  struct Rpy {
    float roll = 0, pitch = 0, yaw = 0;
  } rpyRad;

  uint32_t bytes = 0, frames = 0, checksumErrors = 0, lengthErrors = 0;
  uint32_t invalidValues = 0, partialTimeouts = 0, otherFrames = 0;
  uint32_t quaternionCount = 0, rpyCount = 0;
  uint32_t quaternionMs = 0, rpyMs = 0;
  bool hasQuaternion = false, hasRpy = false;

  bool quaternionFresh(uint32_t now, uint32_t limit = 500) const {
    return hasQuaternion && uint32_t(now - quaternionMs) <= limit;
  }
  bool rpyFresh(uint32_t now, uint32_t limit = 500) const {
    return hasRpy && uint32_t(now - rpyMs) <= limit;
  }
  void feed(uint8_t value, uint32_t now);

 private:
  uint8_t buffer_[64] = {};
  size_t used_ = 0;
  uint32_t lastByteMs_ = 0;
  void discard(size_t count);
  static float readFloat(const uint8_t *data);
  void process(uint32_t now);
};

#ifdef ARDUINO
#include <Arduino.h>
#include "TransportProtocol.h"

class ImuSensor {
 public:
  bool begin();
  void poll();
  bool read(WireProtocol::ImuPayload &sample);
  bool available() const { return available_; }
  void debug() const;

 private:
  HardwareSerial uart_{1};
  ImuParser parser_;
  bool available_ = false;
  bool firstSampleReported_ = false;
  uint32_t emittedQuaternionCount_ = 0;
  uint32_t emittedRpyCount_ = 0;
};
#endif
