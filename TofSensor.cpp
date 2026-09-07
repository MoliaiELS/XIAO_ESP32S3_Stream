#include "TofSensor.h"

bool TofSensor::begin() {
  // TODO: initialize the selected I2C/UART TOF sensor.
  available_ = false;
  return available_;
}

bool TofSensor::read(WireProtocol::TofPayload &sample) {
  (void)sample;
  // TODO: return one real ranging sample when ready.
  return false;
}
