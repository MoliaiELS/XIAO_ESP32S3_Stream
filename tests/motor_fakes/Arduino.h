#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }
inline void delayMicroseconds(unsigned) {}
struct FakeSerial {
  std::string output;
  void printf(const char* fmt, ...) {
    char text[512]; va_list args; va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args); va_end(args); output += text;
  }
};
extern FakeSerial Serial;
