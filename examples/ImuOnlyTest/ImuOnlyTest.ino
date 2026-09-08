#include <Arduino.h>
#include "DmImuParser.h"

// TTL485-V2.0 silk: module TXD (OUTPUT) -> XIAO D7/GPIO44 (RX).
// Verify module supply/logic voltage BEFORE connecting TXD to ESP32.
constexpr int IMU_RX = D7;
constexpr int IMU_TX = D6; // reserved UART output; physically UNCONNECTED
constexpr uint32_t IMU_BAUD = 115200;
constexpr uint8_t IMU_ID = 1;
constexpr uint32_t STALE_MS = 250;
constexpr int USER_LED = 21; // active LOW
HardwareSerial imuPort(1);
DmImuParser imu(IMU_ID);
uint32_t rxBytes = 0, lastRxAt = 0;
bool haveBytes = false, streamEnabled = true;
uint8_t recent[32] = {};
size_t recentNext = 0, recentSize = 0;

void printStatus() {
  uint32_t now = millis();
  Serial.printf("STATUS baud=%lu filter_id=%u RX=GPIO%d rx_bytes=%lu all_crc_valid=%lu matched=%lu other_id=%lu crc_errors=%lu invalid_values=%lu last_id=%u last_reg=0x%02X fresh=%d\n",
    (unsigned long)IMU_BAUD, IMU_ID, IMU_RX, (unsigned long)rxBytes,
    (unsigned long)imu.allFrames, (unsigned long)imu.frames, (unsigned long)imu.otherIdFrames,
    (unsigned long)imu.crcErrors, (unsigned long)imu.invalidValues,
    imu.lastFrameId, imu.lastFrameReg, imu.fresh(now, STALE_MS));
  Serial.printf("RPY_seen=%d QUAT_seen=%d", imu.hasRpy, imu.hasQuaternion);
  if (imu.hasRpy) Serial.printf(" RPY_age_ms=%lu", (unsigned long)(now-imu.rpyAt));
  if (imu.hasQuaternion) Serial.printf(" QUAT_age_ms=%lu", (unsigned long)(now-imu.quaternionAt));
  Serial.println();
}

void printHex() {
  Serial.print("Last RX bytes (oldest first): ");
  for (size_t i = 0; i < recentSize; ++i)
    Serial.printf("%02X ", recent[(recentNext + 32 - recentSize + i) % 32]);
  Serial.println();
}

void setup() {
  pinMode(USER_LED, OUTPUT); digitalWrite(USER_LED, LOW);
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && uint32_t(millis()-start) < 2000) delay(10);
  imuPort.setRxBufferSize(4096);
  imuPort.begin(IMU_BAUD, SERIAL_8N1, IMU_RX, IMU_TX);
  Serial.println("ImuOnlyTest: DM-IMU-L1 RS485 ACTIVE; receive only, no commands sent to IMU.");
  Serial.println("p=status, x=last 32 HEX bytes, o=toggle pose output, h=help");
  Serial.println("LED per 3s: 1=fresh RPY+quat, 2=no recent bytes, 3=bytes but no recent matching pose, 4=only one pose fresh");
  printStatus();
}

void loop() {
  for (int budget = 0; budget < 2048 && imuPort.available(); ++budget) {
    uint8_t b = uint8_t(imuPort.read());
    uint32_t now = millis();
    imu.feed(b, now); ++rxBytes; lastRxAt = now; haveBytes = true;
    recent[recentNext] = b; recentNext = (recentNext + 1) % 32;
    if (recentSize < 32) ++recentSize;
  }
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'p') printStatus();
    else if (c == 'x') printHex();
    else if (c == 'o') { streamEnabled = !streamEnabled; Serial.println(streamEnabled ? "Pose ON" : "Pose OFF"); }
    else if (c == 'h') Serial.println("p=status, x=last 32 HEX bytes, o=toggle pose output; no newline needed");
  }
  uint32_t now = millis();
  bool rpyFresh = imu.hasRpy && uint32_t(now-imu.rpyAt) <= STALE_MS;
  bool quatFresh = imu.hasQuaternion && uint32_t(now-imu.quaternionAt) <= STALE_MS;
  unsigned flashes = rpyFresh && quatFresh ? 1 :
    !haveBytes || uint32_t(now-lastRxAt) > 500 ? 2 : rpyFresh || quatFresh ? 4 : 3;
  uint32_t phase = now % 3000;
  digitalWrite(USER_LED, phase < flashes*300 && phase%300 < 120 ? LOW : HIGH);
  static uint32_t printed = 0, statusAt = 0;
  if (Serial && streamEnabled && rpyFresh && quatFresh && uint32_t(now-printed) >= 100) {
    printed = now;
    Serial.printf("IMU ms=%lu q_wxyz=%.6f,%.6f,%.6f,%.6f rpy_deg=%.3f,%.3f,%.3f age_ms=%lu,%lu\n",
      (unsigned long)now, imu.quaternion[0], imu.quaternion[1], imu.quaternion[2], imu.quaternion[3],
      imu.rpy[0], imu.rpy[1], imu.rpy[2], (unsigned long)(now-imu.quaternionAt), (unsigned long)(now-imu.rpyAt));
  }
  if (Serial && uint32_t(now-statusAt) >= 3000) { statusAt = now; printStatus(); }
  delay(1);
}
