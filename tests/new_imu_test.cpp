#include "../examples/imu/imu.hpp"
#include <vector>
#include <cassert>
#include <limits>
#include <cstdio>
static std::vector<uint8_t> frame(uint8_t fn, std::vector<float> values) {
  std::vector<uint8_t> b={0x7E,0x23,uint8_t(5+values.size()*4),fn};
  for (float v:values) {
    uint32_t bits; memcpy(&bits,&v,4);
    for (int i=0;i<4;++i) b.push_back(uint8_t(bits>>(8*i)));
  }
  uint8_t sum=0; for (auto v:b) sum+=v; b.push_back(sum); return b;
}
static void send(ImuParser& p,const std::vector<uint8_t>& b,uint32_t t) {
  for (auto v:b) p.feed(v,t);
}
int main() {
  ImuParser p, other;
  assert(!p.rpy_fresh(0) && !p.quaternion_fresh(0));
  // Fixed packet: roll=1, pitch=-1, yaw=0, independently encoded.
  send(p,{0x7E,0x23,0x11,0x26,0,0,0x80,0x3F,0,0,0x80,0xBF,0,0,0,0,0xD6},100);
  assert(p.rpy_count==1 && p.rpy_rad.pitch==-1);
  assert(fabs(p.rpy_degrees().roll-57.29578f)<0.0001f);
  auto q=frame(0x16,{1,0,0,0});
  for (size_t i=0;i<q.size();++i) p.feed(q[i],200+uint32_t(i));
  assert(p.quaternion_count==1 && p.quaternion.w==1);
  assert(!other.has_quaternion);
  auto bad=q; bad.back()^=1; send(p,bad,250); send(p,q,251);
  assert(p.checksum_errors==1 && p.quaternion_count==2);
  send(p,{0x7E,0x23,0xFF,0x7E,0x23,0x06,0x26},260);
  send(p,q,261); assert(p.length_errors==2 && p.quaternion_count==3);
  send(p,frame(0x16,{0,0,0,0}),270);
  send(p,frame(0x26,{std::numeric_limits<float>::quiet_NaN(),0,0}),270);
  assert(p.invalid_values==2 && p.rpy_count==1);
  p.feed(0x7E,280); send(p,q,400);
  assert(p.partial_timeouts==1 && p.quaternion_count==4);
  assert(!p.rpy_fresh(601));
  send(p,q,0xFFFFFFF0u); assert(p.quaternion_fresh(0x20));
  send(p,{0x7E,0x23,0x08,0x01,0x01,0x02,0x03,0xB0},500);
  assert(p.has_version && p.version[2]==3);
  puts("new IMU protocol tests passed");
}
