#include <Arduino.h>
#include "imu.hpp"

HardwareSerial imu_uart(1);
Imu imu(imu_uart);
bool print_samples=true;

void setup() {
  Serial.begin(115200);
  const uint32_t start=millis();
  while (!Serial && uint32_t(millis()-start)<2000) delay(10);
  // Module TX -> XIAO D7/GPIO44. Receive only: module RX need not connect.
  // To also send commands: module RX <- D6/GPIO43, change -1 to 43.
  imu.imu_init(44,-1,115200);
  Serial.println("d=debug, p=pause/resume sample prints, v=request version (TX required)");
  Serial.println("Quaternion=w,x,y,z; RPY includes radians and degrees. No calibration commands.");
}

void loop() {
  imu.imu_update();
  if (Serial.available()) {
    const char c=Serial.read();
    if (c=='d') imu.imu_debug();
    else if (c=='p') { print_samples=!print_samples; Serial.println(print_samples?"Print ON":"Print OFF"); }
    else if (c=='v') imu.request_version();
  }
  const uint32_t now=millis();
  static uint32_t last_print=0, last_debug=0;
  if (print_samples && uint32_t(now-last_print)>=100) {
    last_print=now;
    if (imu.quaternion_fresh(now)) {
      const auto& q=imu.quaternion;
      Serial.printf("Q(wxyz): %.5f %.5f %.5f %.5f | age=%lums\n",
        q.w,q.x,q.y,q.z,(unsigned long)(now-imu.quaternion_ms));
    }
    if (imu.rpy_fresh(now)) {
      const auto& r=imu.rpy_rad; const auto d=imu.rpy_degrees();
      Serial.printf("RPY rad: %.4f %.4f %.4f | deg: %.2f %.2f %.2f | age=%lums\n",
        r.roll,r.pitch,r.yaw,d.roll,d.pitch,d.yaw,(unsigned long)(now-imu.rpy_ms));
    }
  }
  if (uint32_t(now-last_debug)>=3000) {
    last_debug=now; imu.imu_debug();
    if (!imu.quaternion_fresh(now) || !imu.rpy_fresh(now))
      Serial.println("Waiting/stale: allow 5s at power-on; check TX->D7, GND, 115200 and output settings.");
  }
  delay(1);
}
