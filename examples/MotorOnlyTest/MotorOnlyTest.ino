#include <Arduino.h>
#include <Wire.h>
#include "HardwareConfig.h"
#include "HapticDriver.h"
#include "MotorDebug.h"

// Standalone motor test: no IMU, Wi-Fi, camera or extra libraries.
HapticDriver motor;

void showStatus() {
  Serial.printf("DRV2605L: %s | ready=%d fault=%d active=%d\n",
    motor.present ? "FOUND at 0x5A" : "NOT FOUND",
    motor.configured, motor.fault, motor.active);
  Serial.printf("Init STATUS read=%d raw=0x%02X device_id=%u\n",
    motor.statusRead, motor.chipStatus, motor.chipStatus >> 5);
  motor.printFault();
}

void showHelp() {
  Serial.println("D4(GPIO5)=SDA, D5(GPIO6)=SCL. LRA ~200Hz, peak ceiling 2.5V.");
  Serial.println("1: weak 150ms | 2: medium 300ms | 3: stronger 300ms");
  Serial.println("s: stop | p: status | d: I2C scan/registers | r: reinitialize + debug | h: help");
  Serial.println("LED bursts: 1=ready 2=no ACK 3=read/ID 4=config/fault 5=Wire failed");
  Serial.println("Send ONE character. Newline is optional. No automatic vibration.");
}

void setup() {
  pinMode(DEBUG_LED, OUTPUT);
  digitalWrite(DEBUG_LED, LOW);
  Serial.begin(115200);
  const uint32_t started = millis();
  while (!Serial && uint32_t(millis() - started) < 2000) delay(10);
  debugBusStarted = Wire.begin(HardwareConfig::SDA_PIN, HardwareConfig::SCL_PIN, 100000);
  Wire.setTimeOut(20);
  if (debugBusStarted) motor.begin();
  showHelp();
  showStatus();
  diagnoseMotorBus(motor);
}

void loop() {
  motor.update(millis());
  updateDebugLed(motor);
  // One character each loop; motor timeout continues while console is idle.
  if (Serial.available()) {
    const char command = Serial.read();
    if (command >= '1' && command <= '3') {
      const uint8_t strength = command == '1' ? 32 : command == '2' ? 64 : 100;
      const uint32_t duration = command == '1' ? 150 : 300;
      if (motor.pulse(strength, duration, millis()))
        Serial.printf("Pulse started: strength=%u/127, duration=%lu ms\n",
                      strength, (unsigned long)duration);
      else { Serial.println("Cannot start. Check wiring and driver status."); showStatus(); }
    } else if (command == 's') {
      Serial.println(motor.stop() ? "Stopped" : "STOP FAILED: disconnect motor power and check I2C");
    } else if (command == 'p') showStatus();
    else if (command == 'd') diagnoseMotorBus(motor);
    else if (command == 'r') {
      if (motor.active) { Serial.println("Send s before reinitializing."); }
      else {
        if (debugBusStarted) motor.begin();
        diagnoseMotorBus(motor); showStatus();
      }
    }
    else if (command == 'h') showHelp();
    else if (command != '\r' && command != '\n' && command != ' ')
      Serial.println("Unknown command. Send h for help.");
  }
  static bool faultReported = false;
  if (motor.fault && !faultReported) {
    Serial.println("Driver/I2C fault: disconnect motor power, check wiring, then restart.");
    motor.printFault();
  }
  faultReported = motor.fault;
  delay(1);
}
