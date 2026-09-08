#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
class TwoWire {
 public:
  uint8_t registers[8][256] = {};
  int beginCalls = 0, channel = 0;
  bool mux = false;
  int failedAddress = -1, failCount = 0;
  bool shortRead = false;
  uint8_t corruptReg = 0xFF;
  TwoWire() { for (auto& bank : registers) bank[0] = 0xE0; }
  bool begin(int, int, unsigned) { ++beginCalls; return true; }
  void setTimeOut(unsigned) {}
  void beginTransmission(uint8_t address) { address_ = address; tx_.clear(); }
  size_t write(uint8_t value) { tx_.push_back(value); return 1; }
  uint8_t endTransmission(bool = true) {
    if (address_ == failedAddress && failCount-- > 0) return 2;
    if (address_ == 0x70 && mux) {
      if (tx_.size() != 1 || !tx_[0] || (tx_[0] & (tx_[0]-1))) return 3;
      for (int i = 0; i < 8; ++i) if (tx_[0] == (1u << i)) channel = i;
      return 0;
    }
    if (address_ != 0x5A) return 2;
    if (tx_.size()) pointer_ = tx_[0];
    if (tx_.size() == 2) registers[channel][pointer_] = tx_[1];
    return 0;
  }
  size_t requestFrom(uint8_t, uint8_t) { return shortRead ? 0 : 1; }
  int read() { return registers[channel][pointer_] ^ (pointer_ == corruptReg ? 1 : 0); }
 private:
  uint8_t address_ = 0, pointer_ = 0;
  std::vector<uint8_t> tx_;
};
