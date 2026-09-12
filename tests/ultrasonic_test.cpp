#include "../UltrasonicSensor.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
static void send(UltrasonicParser& p,std::initializer_list<uint8_t> b,uint32_t t) {
  for(auto v:b) p.feed(v,t);
}
int main() {
  UltrasonicParser p;
  assert(!p.valid(0) && isnan(p.distanceMm(0)));
  send(p,{0xFF,0x07,0xA1,0xA7},100);
  assert(p.valid(100) && p.distanceMm(100)==1953);
  send(p,{0xFF,0xFF,0xFD,0xFB},200);
  assert(p.status==UltrasonicParser::Status::NoTarget && isnan(p.distanceMm(200)));
  send(p,{0xFF,0xFF,0xFE,0xFC},300);
  assert(p.status==UltrasonicParser::Status::Interference);
  send(p,{0x22,0xFF,0x07,0xA1,0x00,0xFF,0x07,0xA1,0xA7},400);
  assert(p.checksumErrors==1 && p.distanceMm(400)==1953);
  // Lost byte followed by a new header, then recovery with FF in data.
  send(p,{0xFF,0x07,0xFF,0xFF,0xFD,0xFB},500);
  assert(p.status==UltrasonicParser::Status::NoTarget);
  p.feed(0xFF,600); send(p,{0xFF,0x07,0xA1,0xA7},700);
  assert(p.partialTimeouts==1 && p.valid(700));
  assert(!p.fresh(2201) && isnan(p.distanceMm(2201)));
  send(p,{0xFF,0x07,0xA1,0xA7},0xFFFFFFF0u); assert(p.valid(20));
  UltrasonicParser us(UltrasonicParser::Unit::Microseconds);
  send(us,{0xFF,0x07,0xA1,0xA7},0);
  assert(fabs(us.distanceMm(0)-1953.0f/5.75f)<0.001f);
  assert(p.raw==1953);
  puts("ultrasonic protocol tests passed");
}
