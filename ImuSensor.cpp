#include "ImuSensor.h"

void ImuParser::discard(size_t count) {
  used_ -= count;
  memmove(buffer_, buffer_ + count, used_);
}

float ImuParser::readFloat(const uint8_t *data) {
  static_assert(sizeof(float) == 4, "Protocol requires float32");
  const uint32_t bits = uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
                        (uint32_t(data[2]) << 16) |
                        (uint32_t(data[3]) << 24);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

void ImuParser::feed(uint8_t value, uint32_t now) {
  ++bytes;
  if (used_ && uint32_t(now - lastByteMs_) > 50) {
    used_ = 0;
    ++partialTimeouts;
  }
  lastByteMs_ = now;
  buffer_[used_++] = value;
  process(now);
}

void ImuParser::process(uint32_t now) {
  while (used_) {
    if (buffer_[0] != 0x7E) {
      discard(1);
      continue;
    }
    if (used_ < 2) return;
    if (buffer_[1] != 0x23) {
      discard(1);
      continue;
    }
    if (used_ < 3) return;
    const uint8_t length = buffer_[2];
    if (length < 5 || length > sizeof(buffer_)) {
      ++lengthErrors;
      discard(1);
      continue;
    }
    if (used_ < 4) return;
    const uint8_t function = buffer_[3];
    const uint8_t expected = function == 0x16 ? 21 : function == 0x26 ? 17 :
                             function == 0x04 ? 23 : function == 0x32 ? 21 :
                             function == 0x01 ? 8 : function == 0x81 ? 7 : 0;
    if (expected && length != expected) {
      ++lengthErrors;
      discard(1);
      continue;
    }
    if (used_ < length) return;
    uint8_t checksum = 0;
    for (size_t index = 0; index < size_t(length - 1); ++index) {
      checksum += buffer_[index];
    }
    if (checksum != buffer_[length - 1]) {
      ++checksumErrors;
      discard(1);
      continue;
    }

    ++frames;
    if (function == 0x16 || function == 0x26) {
      float values[4] = {};
      bool valid = true;
      const size_t count = function == 0x16 ? 4 : 3;
      for (size_t index = 0; index < count; ++index) {
        values[index] = readFloat(buffer_ + 4 + 4 * index);
        valid = valid && isfinite(values[index]);
      }
      if (function == 0x16) {
        const float norm2 = values[0] * values[0] + values[1] * values[1] +
                            values[2] * values[2] + values[3] * values[3];
        valid = valid && norm2 >= 0.5f && norm2 <= 1.5f;
      }
      if (!valid) {
        ++invalidValues;
      } else if (function == 0x16) {
        quaternion.w = values[0];
        quaternion.x = values[1];
        quaternion.y = values[2];
        quaternion.z = values[3];
        hasQuaternion = true;
        quaternionMs = now;
        ++quaternionCount;
      } else {
        rpyRad.roll = values[0];
        rpyRad.pitch = values[1];
        rpyRad.yaw = values[2];
        hasRpy = true;
        rpyMs = now;
        ++rpyCount;
      }
    } else {
      ++otherFrames;
    }
    discard(length);
  }
}

#ifdef ARDUINO
#include "Config.h"

bool ImuSensor::begin() {
  if (!AppConfig::ENABLE_IMU) {
    Serial.println("[IMU] Disabled in Config.h");
    return false;
  }
  parser_ = ImuParser();
  uart_.setRxBufferSize(4096);
  uart_.begin(AppConfig::IMU_BAUD, SERIAL_8N1, AppConfig::IMU_RX_PIN,
              AppConfig::IMU_TX_PIN);
  available_ = static_cast<bool>(uart_);
  Serial.printf("[IMU] UART1 started=%d heap=%lu psram=%lu RX=GPIO%d TX=%d\n",
                available_, static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getPsramSize()),
                AppConfig::IMU_RX_PIN, AppConfig::IMU_TX_PIN);
  if (!available_) {
    Serial.println("[IMU] UART initialization FAILED; enable Core Debug Level=Info for driver errors");
    return false;
  }
  Serial.printf("[IMU] UART1 ready: %lu 8N1, RX=GPIO%d, TX=%d; waiting for 7E 23 frames\n",
                static_cast<unsigned long>(AppConfig::IMU_BAUD),
                AppConfig::IMU_RX_PIN, AppConfig::IMU_TX_PIN);
  return true;
}

void ImuSensor::poll() {
  if (!available_) return;
  for (size_t count = 0; count < 512 && uart_.available(); ++count) {
    const int value = uart_.read();
    if (value >= 0) parser_.feed(static_cast<uint8_t>(value), millis());
  }
  // USB pose output is independent of Wi-Fi/TCP. Emit each new sample at <=10 Hz.
  static uint32_t printedAt = 0, printedCount = 0;
  const uint32_t now = millis();
  if (Serial && parser_.quaternionFresh(now, AppConfig::IMU_STALE_MS) &&
      parser_.quaternionCount != printedCount && uint32_t(now - printedAt) >= 100) {
    printedAt = now;
    printedCount = parser_.quaternionCount;
    const auto &q = parser_.quaternion;
    Serial.printf("Q(wxyz): %.6f %.6f %.6f %.6f | age=%lums\n",
                  q.w, q.x, q.y, q.z,
                  static_cast<unsigned long>(now - parser_.quaternionMs));
  }
  if (!firstSampleReported_ && parser_.hasQuaternion && parser_.hasRpy) {
    firstSampleReported_ = true;
    Serial.println("[IMU] First quaternion and RPY samples received");
  }
}

bool ImuSensor::read(WireProtocol::ImuPayload &sample) {
  const uint32_t now = millis();
  if (!available_ ||
      !parser_.quaternionFresh(now, AppConfig::IMU_STALE_MS) ||
      !parser_.rpyFresh(now, AppConfig::IMU_STALE_MS) ||
      (emittedQuaternionCount_ == parser_.quaternionCount &&
       emittedRpyCount_ == parser_.rpyCount)) {
    return false;
  }
  sample.quaternionWxyz[0] = parser_.quaternion.w;
  sample.quaternionWxyz[1] = parser_.quaternion.x;
  sample.quaternionWxyz[2] = parser_.quaternion.y;
  sample.quaternionWxyz[3] = parser_.quaternion.z;
  sample.rpyRad[0] = parser_.rpyRad.roll;
  sample.rpyRad[1] = parser_.rpyRad.pitch;
  sample.rpyRad[2] = parser_.rpyRad.yaw;
  emittedQuaternionCount_ = parser_.quaternionCount;
  emittedRpyCount_ = parser_.rpyCount;
  return true;
}

void ImuSensor::debug() const {
  Serial.printf("[IMU] driver_started=%d init_ok=%d heap=%lu psram=%lu\n",
                static_cast<bool>(uart_), available_,
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getPsramSize()));
  Serial.printf("[IMU] bytes=%lu frames=%lu q=%lu rpy=%lu checksum=%lu length=%lu invalid=%lu partial=%lu other=%lu\n",
                static_cast<unsigned long>(parser_.bytes),
                static_cast<unsigned long>(parser_.frames),
                static_cast<unsigned long>(parser_.quaternionCount),
                static_cast<unsigned long>(parser_.rpyCount),
                static_cast<unsigned long>(parser_.checksumErrors),
                static_cast<unsigned long>(parser_.lengthErrors),
                static_cast<unsigned long>(parser_.invalidValues),
                static_cast<unsigned long>(parser_.partialTimeouts),
                static_cast<unsigned long>(parser_.otherFrames));
}
#endif
