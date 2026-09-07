#pragma once

#include "TransportProtocol.h"

class TofSensor {
 public:
  bool begin();
  bool read(WireProtocol::TofPayload &sample);
  bool available() const { return available_; }

 private:
  bool available_ = false;
};
