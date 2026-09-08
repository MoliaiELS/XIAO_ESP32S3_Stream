// Arduino example: open this file and upload to XIAO ESP32S3.
#include <Arduino.h>
#include "bsp_motor.hpp"

MotorBus motorBus(Wire); // hardware I2C controller 0, no multiplexer
BspMotor motor1(motorBus, "motor1");

void setup() {
  Serial.begin(115200);
  const uint32_t started = millis();
  while (!Serial && uint32_t(millis() - started) < 2000) delay(10);

  // XIAO board D4 / GPIO5 -> DRV2605L SDA
  // XIAO board D5 / GPIO6 -> DRV2605L SCL
  // 3V3 -> VIN; GND -> GND and IN; OUT+/OUT- -> LRA motor.
  motor1.motor_init(D4, D5);
  Serial.println("MotorApp: 1/2/3=beep preset, s=stop, p=debug, r=reinitialize");
}

void loop() {
  motor1.motor_update(); // required: automatic stop and fault polling
  if (Serial.available()) {
    const char c = Serial.read();
    if (c >= '1' && c <= '3') motor1.motor_beep(uint8_t(c - '0'));
    else if (c == 's') motor1.motor_stop();
    else if (c == 'p') motor1.motor_debug();
    else if (c == 'r') motor1.motor_init(D4, D5);
    else if (c != '\r' && c != '\n' && c != ' ')
      Serial.println("Use 1/2/3/s/p/r");
  }
  delay(1);
}
