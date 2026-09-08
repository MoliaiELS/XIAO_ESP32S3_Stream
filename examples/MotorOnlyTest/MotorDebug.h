#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "HardwareConfig.h"
#include "HapticDriver.h"

// XIAO ESP32S3 USER_LED (not charge LED), active LOW.
constexpr int DEBUG_LED = 21;
bool debugBusStarted = false;
bool debugAddressAck = false;
bool debugReadOk = false;
uint8_t debugStatus = 0;

void updateDebugLed(const HapticDriver& motor) {
  // One burst every 3 seconds: 1=ready, 2=no ACK, 3=read/ID,
  // 4=configuration/runtime fault, 5=Wire.begin failed.
  const int flashes = !debugBusStarted ? 5 : motor.fault ? 4 :
      !debugAddressAck ? 2 : !debugReadOk || !motor.present ? 3 :
      !motor.configured ? 4 : 1;
  const uint32_t phase = millis() % 3000;
  const bool on = motor.active || (phase < uint32_t(flashes * 300) && phase % 300 < 120);
  digitalWrite(DEBUG_LED, on ? LOW : HIGH);
}

bool debugReadRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(0x5A); Wire.write(reg);
  uint8_t error = Wire.endTransmission(false);
  if (error) { Serial.printf("REG 0x%02X pointer write error=%u\n", reg, error); return false; }
  size_t count = Wire.requestFrom(uint8_t(0x5A), uint8_t(1));
  if (count != 1) { Serial.printf("REG 0x%02X read bytes=%u (expected 1)\n", reg, unsigned(count)); return false; }
  value = Wire.read();
  Serial.printf("REG 0x%02X = 0x%02X\n", reg, value);
  return true;
}

void diagnoseMotorBus(HapticDriver& motor) {
  if (motor.active) { Serial.println("Send s before diagnostics."); return; }
  Serial.println("=== MOTOR DEBUG v3: first-fault capture; I2C 100kHz SDA=D4/GPIO5 SCL=D5/GPIO6 ===");
  motor.printFault();
  Serial.printf("Wire.begin=%s; idle digital levels: SDA=%d SCL=%d (normally 1,1)\n",
    debugBusStarted ? "OK" : "FAILED", digitalRead(HardwareConfig::SDA_PIN), digitalRead(HardwareConfig::SCL_PIN));
  debugAddressAck = false; debugReadOk = false;
  if (!debugBusStarted) return;
  unsigned found = 0, nack = 0, other = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (!error) { ++found; Serial.printf("ACK 0x%02X\n", address); }
    else if (error == 2) ++nack;
    else ++other;
    if (address == 0x5A) {
      debugAddressAck = error == 0;
      Serial.printf("Target 0x5A result=%u (0=ACK, 2=address NACK, 5=timeout)\n", error);
    }
    updateDebugLed(motor);
  }
  Serial.printf("Scan: ACK=%u NACK=%u other_errors=%u\n", found, nack, other);
  if (debugAddressAck) {
    debugReadOk = debugReadRegister(0x00, debugStatus);
    if (debugReadOk) Serial.printf("DEVICE_ID=%u (expected 7 or 3), overtemp=%u overcurrent=%u\n",
      debugStatus >> 5, (debugStatus >> 1) & 1, debugStatus & 1);
    const uint8_t registers[] = {0x01, 0x02, 0x17, 0x1A, 0x1D, 0x20};
    for (uint8_t reg : registers) { uint8_t value; debugReadRegister(reg, value); }
  }
  Serial.println("=== END DEBUG (no motor pulse issued) ===");
}
