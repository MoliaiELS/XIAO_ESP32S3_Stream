#include <assert.h>
#include <stdio.h>
#include "../examples/MotorApp/bsp_motor.hpp"
uint32_t fakeMillis = 0;
FakeSerial Serial;

int main() {
  // Two separate buses: timers and stop writes must remain independent.
  TwoWire a, b;
  MotorBus busA(a), busB(b);
  BspMotor m1(busA, "one"), m2(busB, "two");
  assert(m1.motor_init(5, 6) && m2.motor_init(1, 2));
  assert(m1.motor_beep(1) && m2.motor_beep(2));
  fakeMillis = 151; m1.motor_update(); m2.motor_update();
  assert(!m1.motor_active() && m2.motor_active());
  assert(a.registers[0][2] == 0 && b.registers[0][2] == 64);
  fakeMillis = 301; m2.motor_update(); assert(!m2.motor_active());
  assert(!m1.motor_beep(0) && !m1.motor_beep(128, 20));
  // Duplicate direct address must not hijack an existing active motor.
  assert(m1.motor_beep(1));
  BspMotor duplicate(busA, "duplicate");
  assert(!duplicate.motor_init(5, 6)); assert(a.registers[0][2] == 32);
  assert(!m1.motor_init(5, 6)); assert(m1.motor_active());
  m1.motor_stop();
  assert(!m1.motor_init(7, 8)); assert(a.beginCalls == 1);
  assert(m1.motor_init(5, 6));
  // Timestamp wrap still stops correctly.
  fakeMillis = 0xFFFFFFF0u; assert(m1.motor_beep(32, 50));
  fakeMillis = 0x30; m1.motor_update(); assert(!m1.motor_active());
  // Snapshot survives stopping and later debug reads; reinit recovers explicitly.
  assert(m1.motor_beep(1)); a.registers[0][0] = 0xE1;
  fakeMillis += 25; m1.motor_update();
  assert(m1.motor_fault() && !m1.motor_ready() && !m1.motor_active());
  assert(!m1.motor_beep(1));
  assert(Serial.output.find("CHIP_PROTECTION") != std::string::npos);
  assert(Serial.output.find("status=0xE1") != std::string::npos);
  a.registers[0][0] = 0xE0; assert(m1.motor_init(5, 6));
  // Persistent I2C failure must report unconfirmed stop, not silently claim success.
  assert(m1.motor_beep(1)); a.failedAddress = 0x5A; a.failCount = 10;
  fakeMillis += 25; m1.motor_update();
  assert(m1.motor_fault());
  assert(Serial.output.find("stop_confirmed=0 FIRST_ERROR=I2C_READ_POINTER") != std::string::npos);
  // Mux channels share one bus initialization but isolate writes.
  TwoWire w; w.mux = true; MotorBus mux(w, 0x70);
  BspMotor c0(mux, "c0", 0), c1(mux, "c1", 1), c2(mux, "c2", 2);
  assert(c0.motor_init(5, 6) && c1.motor_init(5, 6) && c2.motor_init(5, 6));
  assert(w.beginCalls == 1);
  assert(c0.motor_beep(1) && c1.motor_beep(2) && c2.motor_beep(3));
  assert(w.registers[0][2] == 32 && w.registers[1][2] == 64 && w.registers[2][2] == 100);
  c1.motor_stop(); assert(w.registers[0][2] == 32 && w.registers[1][2] == 0);
  fakeMillis += 151; c0.motor_update(); c2.motor_update();
  assert(!c0.motor_active() && c2.motor_active());
  w.failedAddress = 0x70; w.failCount = 10;
  fakeMillis += 25; c2.motor_update(); assert(c2.motor_fault());
  assert(Serial.output.find("MUX_SELECT") != std::string::npos);
  TwoWire bad; bad.corruptReg = 0x17; MotorBus badBus(bad); BspMotor badMotor(badBus, "bad");
  assert(!badMotor.motor_init(5, 6));
  assert(Serial.output.find("CONFIG_READBACK") != std::string::npos);
  TwoWire shortBus; shortBus.shortRead = true; MotorBus sb(shortBus); BspMotor sm(sb, "short");
  assert(!sm.motor_init(5, 6));
  assert(Serial.output.find("I2C_SHORT_READ") != std::string::npos);
  puts("PASS: independent buses/timers, rollover, bounds, duplicate claims, fault capture/recovery, mux isolation/failure, readback, short reads");
}
