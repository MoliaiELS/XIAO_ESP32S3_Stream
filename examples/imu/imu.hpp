#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

// Protocol: 7E 23 total_length function payload sum8. All floats are LE.
// Each parser owns its state; no shared global UART or sample storage.
class ImuParser {
public:
  struct Quaternion { float w=0, x=0, y=0, z=0; } quaternion;
  struct Rpy { float roll=0, pitch=0, yaw=0; } rpy_rad;
  uint32_t bytes=0, frames=0, checksum_errors=0, length_errors=0;
  uint32_t invalid_values=0, partial_timeouts=0, other_frames=0;
  uint32_t quaternion_count=0, rpy_count=0, quaternion_ms=0, rpy_ms=0;
  bool has_quaternion=false, has_rpy=false, has_version=false;
  uint8_t version[3]={0,0,0};

  bool quaternion_fresh(uint32_t now, uint32_t limit=500) const {
    return has_quaternion && uint32_t(now-quaternion_ms)<=limit;
  }
  bool rpy_fresh(uint32_t now, uint32_t limit=500) const {
    return has_rpy && uint32_t(now-rpy_ms)<=limit;
  }
  Rpy rpy_degrees() const {
    Rpy v; const float k=57.2957795131f;
    v.roll=rpy_rad.roll*k; v.pitch=rpy_rad.pitch*k; v.yaw=rpy_rad.yaw*k;
    return v;
  }
  void feed(uint8_t value, uint32_t now) {
    ++bytes;
    if (used && uint32_t(now-last_byte_ms)>50) { used=0; ++partial_timeouts; }
    last_byte_ms=now;
    buffer[used++]=value;
    process(now);
  }
private:
  uint8_t buffer[64]={0};
  size_t used=0;
  uint32_t last_byte_ms=0;
  void discard(size_t n) { used-=n; memmove(buffer,buffer+n,used); }
  static float read_float(const uint8_t* p) {
    static_assert(sizeof(float)==4,"Protocol requires float32");
    uint32_t bits=uint32_t(p[0]) | (uint32_t(p[1])<<8) |
      (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
    float v; memcpy(&v,&bits,4); return v;
  }
  void process(uint32_t now) {
    while (used) {
      if (buffer[0]!=0x7E) { discard(1); continue; }
      if (used<2) return;
      if (buffer[1]!=0x23) { discard(1); continue; }
      if (used<3) return;
      const uint8_t n=buffer[2];
      if (n<5 || n>sizeof(buffer)) { ++length_errors; discard(1); continue; }
      if (used<4) return;
      const uint8_t fn=buffer[3];
      const uint8_t expected=fn==0x16?21:fn==0x26?17:fn==0x04?23:
        fn==0x32?21:fn==0x01?8:fn==0x81?7:0;
      if (expected && n!=expected) { ++length_errors; discard(1); continue; }
      if (used<n) return;
      uint8_t sum=0;
      for (size_t i=0;i<size_t(n-1);++i) sum+=buffer[i];
      if (sum!=buffer[n-1]) { ++checksum_errors; discard(1); continue; }
      ++frames;
      if (fn==0x16 || fn==0x26) {
        float values[4]={0,0,0,0}; bool valid=true;
        const size_t count=fn==0x16?4:3;
        for (size_t i=0;i<count;++i) {
          values[i]=read_float(buffer+4+4*i);
          valid=valid && isfinite(values[i]);
        }
        // Reject obviously invalid quaternions, but do not alter valid samples.
        if (fn==0x16) {
          const float norm2=values[0]*values[0]+values[1]*values[1]+
            values[2]*values[2]+values[3]*values[3];
          valid=valid && norm2>=0.5f && norm2<=1.5f;
        }
        if (!valid) ++invalid_values;
        else if (fn==0x16) {
          quaternion.w=values[0]; quaternion.x=values[1];
          quaternion.y=values[2]; quaternion.z=values[3];
          has_quaternion=true; quaternion_ms=now; ++quaternion_count;
        } else {
          rpy_rad.roll=values[0]; rpy_rad.pitch=values[1]; rpy_rad.yaw=values[2];
          has_rpy=true; rpy_ms=now; ++rpy_count;
        }
      } else if (fn==0x01) {
        memcpy(version,buffer+4,3); has_version=true;
      } else ++other_frames; // Accel/gyro/mag/barometer/ACK: validated, not decoded.
      discard(n);
    }
  }
};

#ifdef ARDUINO
#include <Arduino.h>

class Imu : public ImuParser {
public:
  explicit Imu(HardwareSerial& uart, const char* label="IMU") : port(uart), name(label) {}
  // XIAO: RX pin D7=GPIO44 <- module TX; TX pin D6=GPIO43 -> module RX.
  // For receive-only wiring pass tx_pin=-1 and leave module RX disconnected.
  void imu_init(int rx_pin=44, int tx_pin=-1, uint32_t baud=115200) {
    static_cast<ImuParser&>(*this)=ImuParser();
    can_transmit=tx_pin>=0;
    port.begin(baud,SERIAL_8N1,rx_pin,tx_pin);
    Serial.printf("[%s] UART %lu 8N1 RX=GPIO%d TX=%d; allow 5s startup\n",
      name,(unsigned long)baud,rx_pin,tx_pin);
  }
  void imu_update() {
    // Bound work so other devices can still run in the main loop.
    for (size_t n=0;n<512 && port.available();++n) {
      int b=port.read(); if (b>=0) feed(uint8_t(b),millis());
    }
  }
  void imu_debug() const {
    Serial.printf("[%s] bytes=%lu frames=%lu q=%lu rpy=%lu checksum=%lu length=%lu invalid=%lu partial=%lu other=%lu\n",
      name,(unsigned long)bytes,(unsigned long)frames,(unsigned long)quaternion_count,
      (unsigned long)rpy_count,(unsigned long)checksum_errors,(unsigned long)length_errors,
      (unsigned long)invalid_values,(unsigned long)partial_timeouts,(unsigned long)other_frames);
    Serial.printf("[%s] fresh: quaternion=%d RPY=%d\n",name,
      quaternion_fresh(millis()),rpy_fresh(millis()));
    if (has_version) Serial.printf("[%s] version=%u.%u.%u\n",name,version[0],version[1],version[2]);
  }
  void request_version() {
    if (!can_transmit) {
      Serial.println("Version request needs module RX wired and TX pin enabled in imu_init."); return;
    }
    const uint8_t command[]={0x7E,0x23,0x07,0x80,0x01,0x00,0x29};
    port.write(command,sizeof(command));
    Serial.printf("[%s] Version requested; send d to inspect response.\n",name);
  }
private:
  HardwareSerial& port;
  const char* name;
  bool can_transmit=false;
};
#endif
