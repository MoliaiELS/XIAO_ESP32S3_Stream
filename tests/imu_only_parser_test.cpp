#include "../examples/ImuOnlyTest/DmImuParser.h"
#include <assert.h>
#include <vector>
#include <stdio.h>
#include <limits>

static std::vector<uint8_t> frame(uint8_t reg, std::vector<float> values, uint8_t id = 1) {
  std::vector<uint8_t> b = {0x55, 0xAA, id, reg};
  for (float value : values) {
    uint32_t bits; memcpy(&bits, &value, 4);
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(bits >> (8 * i)));
  }
  uint16_t crc = DmImuParser::crc16(b.data(), b.size());
  b.push_back(uint8_t(crc)); b.push_back(uint8_t(crc >> 8)); b.push_back(0x0A);
  return b;
}
static void send(DmImuParser& p, const std::vector<uint8_t>& b, uint32_t t) {
  for (auto v : b) p.feed(v, t);
}
int main() {
  const uint8_t check[] = "123456789";
  assert(DmImuParser::crc16(check, 9) == 0x29B1);
  DmImuParser p;
  assert(!p.fresh(0, 250));
  auto euler = frame(3, {12.5f, -23.0f, 179.0f});
  auto q = frame(4, {1, 0, 0, 0});
  send(p, {0x00, 0x55, 0x55, 0xAA, 0x01, 0xFF}, 1);
  send(p, euler, 10); send(p, q, 11);
  assert(p.fresh(11, 250) && p.rpy[1] == -23 && p.quaternion[0] == 1);
  assert(!p.fresh(261, 250));
  auto bad = euler; bad[5] ^= 1; send(p, bad, 12);
  assert(p.crcErrors > 0 && p.rpyAt == 10);
  bad = euler; bad.back() = 0; send(p, bad, 13);
  send(p, frame(3, {99, 99, 99}, 2), 14); assert(p.rpyAt == 10);
  assert(p.otherIdFrames == 1 && p.lastFrameId == 2 && p.lastFrameReg == 3 && p.allFrames == 3);
  send(p, frame(3, {std::numeric_limits<float>::quiet_NaN(), 0, 0}), 15);
  send(p, frame(4, {0, 0, 0, 0}), 16);
  assert(p.invalidValues == 2 && p.quaternionAt == 11);
  // Every possible single-byte deletion must recover on subsequent good frames.
  for (size_t i = 0; i < euler.size(); ++i) {
    DmImuParser recovery;
    auto truncated = euler; truncated.erase(truncated.begin() + i);
    send(recovery, truncated, 20); send(recovery, euler, 21);
    send(recovery, q, 22); send(recovery, euler, 23);
    assert(recovery.fresh(23, 250) && recovery.rpy[0] == 12.5f);
  }
  send(p, {0x55, 0xAA, 1, 4, 0}, 30); send(p, euler, 100); send(p, q, 101);
  assert(p.fresh(101, 250));
  // Payload bytes may contain header/tail values; CRC and length delimit frames.
  uint32_t bits = 0x3F0AAA55; float special; memcpy(&special, &bits, 4);
  send(p, frame(3, {special, 0, 0}), 102); assert(p.rpy[0] == special);
  send(p, euler, 0xFFFFFFF0u); send(p, q, 0xFFFFFFF1u);
  assert(p.fresh(0x10, 250)); assert(!p.fresh(0x200, 250));
  puts("PASS CRC vector, RPY/quaternion, noise, bad CRC/tail, ID, NaN, zero quaternion, byte loss, partial timeout, embedded markers, millis rollover");
}
