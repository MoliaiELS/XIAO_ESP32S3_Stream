#pragma once

#include <WiFi.h>
#include "TransportProtocol.h"

struct ReceivedFrame {
  WireProtocol::MessageType type = WireProtocol::MessageType::Command;
  uint16_t flags = 0;
  uint32_t sequence = 0;
  size_t payloadLength = 0;
  uint8_t payload[WireProtocol::MAX_RX_PAYLOAD] = {};
};

class TransportService {
 public:
  explicit TransportService(uint16_t port);
  void begin();
  void poll();
  bool connected();
  bool popReceived(ReceivedFrame &frame);
  bool sendFrame(WireProtocol::MessageType type, const void *payload, size_t length,
                 uint16_t flags = WireProtocol::FLAG_NONE);

 private:
  bool writeAll(const uint8_t *data, size_t length);
  void resetReceive();
  void readIncoming();

  WiFiServer server_;
  uint16_t port_;
  WiFiClient client_;
  bool started_ = false;
  bool pendingFrame_ = false;
  uint32_t txSequence_ = 0;
  uint8_t headerBuffer_[sizeof(WireProtocol::FrameHeader)] = {};
  size_t headerBytes_ = 0;
  WireProtocol::FrameHeader rxHeader_ = {};
  size_t payloadBytes_ = 0;
  ReceivedFrame received_;
};
