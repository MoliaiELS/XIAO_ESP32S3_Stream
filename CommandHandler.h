#pragma once

#include "SensorManager.h"
#include "TransportService.h"

class CommandHandler {
 public:
  static void handle(const ReceivedFrame &frame, TransportService &transport,
                     SensorManager &sensors);
};
