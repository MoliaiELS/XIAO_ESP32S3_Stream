#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

// DYP-A22YYUW-V1.0, specification sections 3.1 and 3.6.
class UltrasonicParser {
public:
  enum class Unit { Millimetres, Microseconds };
  enum class Status { Waiting, Distance, NoTarget, Interference, Invalid };
  explicit UltrasonicParser(Unit unit=Unit::Millimetres) : unit_(unit) {}
  uint32_t bytes=0, frames=0, checksum_errors=0, partial_timeouts=0;
  uint32_t sample_ms=0, no_target_count=0, interference_count=0;
  uint16_t raw=0;
  Status status=Status::Waiting;
  bool fresh(uint32_t now, uint32_t timeout=1500) const {
    return frames!=0 && uint32_t(now-sample_ms)<=timeout;
  }
  bool valid(uint32_t now) const { return fresh(now) && status==Status::Distance; }
  // NAN for no target / interference / stale: never return these as a distance.
  float distance_mm(uint32_t now) const {
    return valid(now) ? (unit_==Unit::Millimetres ? float(raw) : float(raw)/5.75f) : NAN;
  }
  const char* status_text() const {
    switch(status) {
      case Status::Distance: return "DISTANCE";
      case Status::NoTarget: return "NO_TARGET";
      case Status::Interference: return "INTERFERENCE";
      case Status::Invalid: return "INVALID_VALUE";
      default: return "WAITING";
    }
  }
  void feed(uint8_t b, uint32_t now) {
    ++bytes;
    if (used_ && uint32_t(now-last_byte_)>50) { used_=0; ++partial_timeouts; }
    last_byte_=now;
    if (!used_ && b!=0xFF) return;
    buffer_[used_++]=b;
    if (used_<4) return;
    if (uint8_t(buffer_[0]+buffer_[1]+buffer_[2])!=buffer_[3]) {
      ++checksum_errors;
      // Retain a possible header inside the failed packet (including FF data).
      size_t next=1; while (next<4 && buffer_[next]!=0xFF) ++next;
      used_=4-next; memmove(buffer_,buffer_+next,used_); return;
    }
    raw=(uint16_t(buffer_[1])<<8)|buffer_[2];
    ++frames; sample_ms=now; used_=0;
    if (raw==0xFFFD) { status=Status::NoTarget; ++no_target_count; }
    else if (raw==0xFFFE) { status=Status::Interference; ++interference_count; }
    else if (raw==0 || raw==0xFFFF) status=Status::Invalid;
    else status=Status::Distance;
  }
private:
  Unit unit_;
  uint8_t buffer_[4]={0};
  size_t used_=0;
  uint32_t last_byte_=0;
};

#ifdef ARDUINO
#include <Arduino.h>
class Ultrasonic : public UltrasonicParser {
public:
  explicit Ultrasonic(HardwareSerial& uart) : uart_(uart) {}
  // XIAO D2/GPIO3 <- sensor TX. Sensor RX floats (processed output).
  // UART2 is selected by the caller; IMU uses UART1. No transmission is made.
  void ultrasonic_init(int rx_pin=3, Unit unit=Unit::Millimetres, uint32_t baud=115200) {
    static_cast<UltrasonicParser&>(*this)=UltrasonicParser(unit);
    uart_.begin(baud,SERIAL_8N1,rx_pin,-1);
    Serial.printf("[A22] UART AUTO RX=GPIO%d baud=%lu 8N1; wait >=650ms\n",
      rx_pin,(unsigned long)baud);
    Serial.printf("[A22] Configured payload unit=%s; must match sensor register 0x0209\n",
      unit==Unit::Millimetres?"mm":"us (mm=us/5.75)");
  }
  void ultrasonic_update() {
    for (size_t n=0;n<256 && uart_.available();++n) {
      int b=uart_.read(); if (b>=0) feed(uint8_t(b),millis());
    }
  }
  void ultrasonic_debug() const {
    Serial.printf("[A22] bytes=%lu frames=%lu checksum=%lu partial=%lu no_target=%lu interference=%lu status=%s fresh=%d raw=0x%04X\n",
      (unsigned long)bytes,(unsigned long)frames,(unsigned long)checksum_errors,
      (unsigned long)partial_timeouts,(unsigned long)no_target_count,
      (unsigned long)interference_count,status_text(),fresh(millis()),raw);
  }
private:
  HardwareSerial& uart_;
};
#endif
