#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

// DM-IMU-L1 V1.2 USB / RS485 ACTIVE protocol. No Arduino dependency.
class DmImuParser {
 public:
  explicit DmImuParser(uint8_t id = 1) : id_(id) {}
  float rpy[3] = {};  // roll, pitch, yaw, degrees (vendor Euler frame)
  float quaternion[4] = {};  // w, x, y, z (vendor quaternion frame)
  uint32_t rpyAt = 0, quaternionAt = 0;
  bool hasRpy = false, hasQuaternion = false;
  uint32_t frames = 0, crcErrors = 0, invalidValues = 0;
  uint32_t allFrames = 0, otherIdFrames = 0;
  uint8_t lastFrameId = 0, lastFrameReg = 0;

  static uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
      crc ^= static_cast<uint16_t>(data[i]) << 8;
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
  }

  void feed(uint8_t byte, uint32_t now) {
    // Recover from an interrupted frame, including after sensor unplug/replug.
    if (used_ && static_cast<uint32_t>(now - lastByteAt_) > 50) used_ = 0;
    lastByteAt_ = now;
    buffer_[used_++] = byte;
    while (used_) {
      if (buffer_[0] != 0x55 || (used_ >= 2 && buffer_[1] != 0xAA)) {
        discard(1); continue;
      }
      if (used_ < 4) return;
      const uint8_t reg = buffer_[3];
      if (reg < 1 || reg > 4) { discard(1); continue; }
      const size_t length = reg == 4 ? 23 : 19;
      if (used_ < length) return;
      const uint16_t received = buffer_[length - 3] |
          (static_cast<uint16_t>(buffer_[length - 2]) << 8);
      if (buffer_[length - 1] != 0x0A || crc16(buffer_, length - 3) != received) {
        ++crcErrors; discard(1); continue;
      }
      ++allFrames; lastFrameId = buffer_[2]; lastFrameReg = reg;
      if (buffer_[2] != id_) ++otherIdFrames;
      if (buffer_[2] == id_) {
        ++frames;
        if (reg == 3 || reg == 4) {
          float values[4] = {};
          const size_t count = reg == 4 ? 4 : 3;
          bool valid = true;
          for (size_t i = 0; i < count; ++i) {
            const uint8_t* p = buffer_ + 4 + 4 * i;
            uint32_t bits = uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                            (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
            static_assert(sizeof(float) == 4, "IEEE754 float32 required");
            memcpy(&values[i], &bits, 4);
            valid = valid && isfinite(values[i]);
          }
          if (reg == 4) {
            float norm2 = 0;
            for (float v : values) norm2 += v * v;
            valid = valid && norm2 > 0.5f && norm2 < 1.5f;
          }
          if (!valid) ++invalidValues;
          else if (reg == 3) {
            memcpy(rpy, values, sizeof(rpy)); rpyAt = now; hasRpy = true;
          } else {
            memcpy(quaternion, values, sizeof(quaternion));
            quaternionAt = now; hasQuaternion = true;
          }
        }
      }
      discard(length);
    }
  }
  bool fresh(uint32_t now, uint32_t timeout) const {
    return hasRpy && hasQuaternion && uint32_t(now - rpyAt) <= timeout &&
           uint32_t(now - quaternionAt) <= timeout;
  }

 private:
  uint8_t id_, buffer_[23] = {};
  size_t used_ = 0;
  uint32_t lastByteAt_ = 0;
  void discard(size_t n) {
    used_ -= n;
    memmove(buffer_, buffer_ + n, used_);
  }
};
