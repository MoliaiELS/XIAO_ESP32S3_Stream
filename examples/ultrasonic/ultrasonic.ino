#include <Arduino.h>
#include "ultrasonic.hpp"

HardwareSerial ultrasonic_uart(2); // UART1 reserved for IMU; USB Serial for debug.
Ultrasonic ultrasonic(ultrasonic_uart);
bool print_samples=true;

void setup() {
  Serial.begin(115200);
  const uint32_t start=millis();
  while (!Serial && uint32_t(millis()-start)<2000) delay(10);
  // Module TX -> D2/GPIO3. RX floats. D3/GPIO4 remains free.
  // Assumption: sensor register 0x0209=0 (mm). For value 1 select Microseconds.
  ultrasonic.ultrasonic_init(3,UltrasonicParser::Unit::Millimetres);
  Serial.println("d=debug, p=pause/resume prints. UART automatic; no commands sent.");
}

void loop() {
  ultrasonic.ultrasonic_update();
  if (Serial.available()) {
    const char c=Serial.read();
    if (c=='d') ultrasonic.ultrasonic_debug();
    else if (c=='p') { print_samples=!print_samples; Serial.println(print_samples?"Print ON":"Print OFF"); }
  }
  const uint32_t now=millis();
  static uint32_t printed_frames=0, last_debug=0;
  if (ultrasonic.frames!=printed_frames) {
    printed_frames=ultrasonic.frames;
    if (print_samples) {
      if (ultrasonic.valid(now)) {
        const float mm=ultrasonic.distance_mm(now);
        Serial.printf("[A22] %.1f mm | %.2f cm | raw=%u\n",mm,mm/10.0f,ultrasonic.raw);
      } else Serial.printf("[A22] %s raw=0x%04X\n",ultrasonic.status_text(),ultrasonic.raw);
    }
  }
  if (uint32_t(now-last_debug)>=3000) {
    last_debug=now; ultrasonic.ultrasonic_debug();
    if (!ultrasonic.fresh(now))
      Serial.println("[A22] NO DATA / STALE: check power, GND, TX->D2, baud and UART AUTO model.");
  }
  delay(1);
}
