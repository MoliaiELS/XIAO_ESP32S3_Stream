#include "UltrasonicSensor.h"

const char *UltrasonicParser::statusText() const {
  switch (status) {
    case Status::Distance: return "DISTANCE";
    case Status::NoTarget: return "NO_TARGET";
    case Status::Interference: return "INTERFERENCE";
    case Status::Invalid: return "INVALID_VALUE";
    default: return "WAITING";
  }
}

void UltrasonicParser::feed(uint8_t value, uint32_t now) {
  ++bytes;
  if (used_ && uint32_t(now - lastByteMs_) > 50) {
    used_ = 0;
    ++partialTimeouts;
  }
  lastByteMs_ = now;
  if (!used_ && value != 0xFF) return;
  buffer_[used_++] = value;
  if (used_ < sizeof(buffer_)) return;

  if (uint8_t(buffer_[0] + buffer_[1] + buffer_[2]) != buffer_[3]) {
    ++checksumErrors;
    size_t next = 1;
    while (next < sizeof(buffer_) && buffer_[next] != 0xFF) ++next;
    used_ = sizeof(buffer_) - next;
    memmove(buffer_, buffer_ + next, used_);
    return;
  }

  raw = (uint16_t(buffer_[1]) << 8) | buffer_[2];
  ++frames;
  sampleMs = now;
  used_ = 0;
  if (raw == 0xFFFD) {
    status = Status::NoTarget;
    ++noTargetCount;
  } else if (raw == 0xFFFE) {
    status = Status::Interference;
    ++interferenceCount;
  } else if (raw == 0 || raw == 0xFFFF) {
    status = Status::Invalid;
  } else {
    status = Status::Distance;
  }
}

#ifdef ARDUINO
#include "Config.h"

bool UltrasonicSensor::begin() {
  if (!AppConfig::ENABLE_ULTRASONIC) {
    Serial.println("[A22] Disabled in Config.h");
    return false;
  }
  const UltrasonicParser::Unit unit =
      AppConfig::ULTRASONIC_VALUES_ARE_MICROSECONDS
          ? UltrasonicParser::Unit::Microseconds
          : UltrasonicParser::Unit::Millimetres;
  parser_ = UltrasonicParser(unit);
  uart_.setRxBufferSize(1024);
  uart_.begin(AppConfig::ULTRASONIC_BAUD, SERIAL_8N1,
              AppConfig::ULTRASONIC_RX_PIN, -1);
  available_ = true;
  Serial.printf("[A22] UART2 ready: %lu 8N1, RX=GPIO%d, unit=%s; wait >=650ms\n",
                static_cast<unsigned long>(AppConfig::ULTRASONIC_BAUD),
                AppConfig::ULTRASONIC_RX_PIN,
                AppConfig::ULTRASONIC_VALUES_ARE_MICROSECONDS ? "us" : "mm");
  return true;
}

void UltrasonicSensor::poll() {
  if (!available_) return;
  for (size_t count = 0; count < 256 && uart_.available(); ++count) {
    const int value = uart_.read();
    if (value >= 0) parser_.feed(static_cast<uint8_t>(value), millis());
  }
  if (!firstFrameReported_ && parser_.frames) {
    firstFrameReported_ = true;
    Serial.printf("[A22] First frame received: status=%s raw=0x%04X\n",
                  parser_.statusText(), parser_.raw);
  }
}

bool UltrasonicSensor::read(WireProtocol::UltrasonicPayload &sample) {
  if (!available_ || emittedFrames_ == parser_.frames) return false;
  const uint32_t now = millis();
  if (!parser_.fresh(now, AppConfig::ULTRASONIC_STALE_MS)) return false;

  sample.distanceMm = 0xFFFF;
  if (parser_.valid(now, AppConfig::ULTRASONIC_STALE_MS)) {
    const float distance = parser_.distanceMm(now, AppConfig::ULTRASONIC_STALE_MS);
    sample.distanceMm = static_cast<uint16_t>(distance + 0.5f);
  }
  sample.rawValue = parser_.raw;
  sample.status = static_cast<uint8_t>(parser_.status);
  sample.unit = static_cast<uint8_t>(parser_.unit());
  sample.reserved = 0;
  emittedFrames_ = parser_.frames;
  return true;
}

void UltrasonicSensor::debug() const {
  Serial.printf("[A22] bytes=%lu frames=%lu checksum=%lu partial=%lu no_target=%lu interference=%lu status=%s fresh=%d raw=0x%04X\n",
                static_cast<unsigned long>(parser_.bytes),
                static_cast<unsigned long>(parser_.frames),
                static_cast<unsigned long>(parser_.checksumErrors),
                static_cast<unsigned long>(parser_.partialTimeouts),
                static_cast<unsigned long>(parser_.noTargetCount),
                static_cast<unsigned long>(parser_.interferenceCount),
                parser_.statusText(),
                parser_.fresh(millis(), AppConfig::ULTRASONIC_STALE_MS),
                parser_.raw);
}
#endif
