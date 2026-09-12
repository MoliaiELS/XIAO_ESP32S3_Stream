#include <assert.h>
#include <stdio.h>

#include "../HapticMotor.h"

uint32_t fakeMillis = 0;
FakeSerial Serial;
TwoWire Wire;

int main() {
  TwoWire bus;
  HapticMotor motor(bus);
  assert(motor.begin(5, 6, 2.5f, 200.0f));
  assert(motor.ready() && !motor.active() && !motor.fault());
  assert(!motor.beep(0));
  assert(!motor.pulse(128, 20));

  assert(motor.beep(1));
  assert(motor.active() && bus.registers[0][2] == 32);
  fakeMillis = 151;
  motor.update();
  assert(!motor.active() && bus.registers[0][2] == 0);

  fakeMillis = 0xFFFFFFF0u;
  assert(motor.pulse(64, 50));
  fakeMillis = 0x30;
  motor.update();
  assert(!motor.active());

  assert(motor.beep(1));
  bus.registers[0][0] = 0xE1;
  fakeMillis += 25;
  motor.update();
  assert(motor.fault() && !motor.ready() && !motor.active());
  assert(Serial.output.find("CHIP_PROTECTION") != std::string::npos);

  bus.registers[0][0] = 0xE0;
  assert(motor.begin(5, 6, 2.5f, 200.0f));
  assert(motor.ready());
  puts("haptic motor tests passed");
}
