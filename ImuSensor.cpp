#include "ImuSensor.h"

bool ImuSensor::begin() {
  // TODO: initialize the selected wearable IMU and set available_ on success.
  available_ = false;
  return available_;
}

bool ImuSensor::read(WireProtocol::ImuPayload &sample) {
  (void)sample;
  // TODO: return one real SI-unit sample when the IMU data-ready flag is set.
  return false;
}
