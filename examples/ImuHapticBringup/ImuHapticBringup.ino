#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>
#include "HardwareConfig.h"
#include "DmImuParser.h"
#include "HapticDriver.h"

HardwareSerial imuSerial(1);
DmImuParser imu(HardwareConfig::IMU_ID);
HapticDriver motor;
bool streamEnabled = true;
uint32_t receivedBytes = 0;

void printStatus() {
  Serial.printf("STATUS rx_bytes=%lu frames=%lu crc_errors=%lu invalid=%lu imu=%s drv=%s configured=%d fault=%d active=%d\n",
    (unsigned long)receivedBytes, (unsigned long)imu.frames,
    (unsigned long)imu.crcErrors, (unsigned long)imu.invalidValues,
    imu.fresh(millis(), HardwareConfig::STALE_MS) ? "FRESH" : "WAIT/STALE",
    motor.present ? "FOUND" : "MISSING", motor.configured, motor.fault, motor.active);
}

void handleCommand(const char* line) {
  if (!strcmp(line, "help")) {
    Serial.println("help | status | stream on | stream off | init | stop | buzz <1..127> <1..1000 ms>");
  } else if (!strcmp(line, "status")) printStatus();
  else if (!strcmp(line, "stream on")) streamEnabled = true;
  else if (!strcmp(line, "stream off")) streamEnabled = false;
  else if (!strcmp(line, "init")) { motor.begin(); printStatus(); }
  else if (!strcmp(line, "stop")) Serial.println(motor.stop() ? "OK stopped" : "ERROR I2C stop failed; disconnect motor power");
  else {
    int amplitude = 0, duration = 0; char extra = 0;
    if (sscanf(line, "buzz %d %d %c", &amplitude, &duration, &extra) == 2 &&
        amplitude >= 1 && amplitude <= 127 && duration >= 1 &&
        duration <= int(HardwareConfig::MAX_PULSE_MS)) {
      Serial.println(motor.pulse(uint8_t(amplitude), uint32_t(duration), millis()) ?
        "OK pulse started" : "ERROR check motor configuration, I2C and status");
    } else Serial.println("ERROR invalid command; type help");
  }
}

void pollConsole() {
  static char line[80]; static size_t used = 0; static bool overflow = false;
  for (int budget = 0; budget < 80 && Serial.available(); ++budget) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (overflow) Serial.println("ERROR command too long");
      else if (used) { line[used] = 0; handleCommand(line); }
      used = 0; overflow = false;
    } else if (!overflow) {
      if (used + 1 < sizeof(line)) line[used++] = c;
      else overflow = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 2000) delay(10);
  Wire.begin(HardwareConfig::SDA_PIN, HardwareConfig::SCL_PIN, 100000);
  Wire.setTimeOut(20);
  imuSerial.setRxBufferSize(4096);
  imuSerial.begin(HardwareConfig::IMU_BAUD, SERIAL_8N1,
                  HardwareConfig::IMU_RX, HardwareConfig::IMU_TX);
  motor.begin();
  Serial.println("DM-IMU-L1 RS485 + DRV2605L bringup; type help (newline required).");
  printStatus();
}

void loop() {
  motor.update(millis());
  for (int budget = 0; budget < 1024 && imuSerial.available(); ++budget) {
    imu.feed(uint8_t(imuSerial.read()), millis()); ++receivedBytes;
  }
  pollConsole();
  motor.update(millis());
  static uint32_t printed = 0, statusAt = 0;
  uint32_t now = millis();
  if (Serial && streamEnabled && now - printed >= 100 &&
      imu.fresh(now, HardwareConfig::STALE_MS)) {
    printed = now;
    Serial.printf("IMU ms=%lu q_wxyz=%.6f,%.6f,%.6f,%.6f rpy_deg=%.3f,%.3f,%.3f age_ms=%lu,%lu\n",
      (unsigned long)now, imu.quaternion[0], imu.quaternion[1], imu.quaternion[2], imu.quaternion[3],
      imu.rpy[0], imu.rpy[1], imu.rpy[2], (unsigned long)(now - imu.quaternionAt),
      (unsigned long)(now - imu.rpyAt));
  }
  if (Serial && now - statusAt >= 3000) { statusAt = now; printStatus(); }
  delay(1);
}
