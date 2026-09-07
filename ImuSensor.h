#pragma once

#include "TransportProtocol.h"

class ImuSensor {
 public:
  bool begin();
  bool read(WireProtocol::ImuPayload &sample);
  bool available() const { return available_; }

 private:
  bool available_ = false;
};
