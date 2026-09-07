#pragma once

#include "TransportService.h"

class CommandHandler {
 public:
  // Command payload is UTF-8 JSON. Add command-name dispatch here later.
  static void handle(const ReceivedFrame &frame, TransportService &transport);
};
