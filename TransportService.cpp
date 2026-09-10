#include "TransportService.h"

#include <cstring>
#include "esp_timer.h"

TransportService::TransportService(uint16_t port) : server_(port), port_(port) {}

void TransportService::begin() {
  if (started_) return;
  server_.begin();
  server_.setNoDelay(true);
  started_ = true;
  Serial.printf("TCP data server listening on port %u\n", port_);
}

bool TransportService::connected() { return client_ && client_.connected(); }

void TransportService::poll() {
  if (!started_) return;

  WiFiClient incoming = server_.accept();
  if (incoming) {
    if (connected()) {
      incoming.stop();  // One laptop client at a time.
    } else {
      client_ = incoming;
      client_.setNoDelay(true);
      resetReceive();
      Serial.printf("Laptop data client connected: remote=%s:%u, local=%s:%u\n",
                    client_.remoteIP().toString().c_str(), client_.remotePort(),
                    client_.localIP().toString().c_str(), client_.localPort());
    }
  }

  if (client_ && !client_.connected()) {
    client_.stop();
    resetReceive();
    Serial.println("Laptop data client disconnected");
    return;
  }

  if (connected() && !pendingFrame_) readIncoming();
}

void TransportService::readIncoming() {
  while (client_.available() > 0 && !pendingFrame_) {
    if (headerBytes_ < sizeof(headerBuffer_)) {
      const size_t wanted = sizeof(headerBuffer_) - headerBytes_;
      const int count = client_.read(headerBuffer_ + headerBytes_, wanted);
      if (count <= 0) return;
      headerBytes_ += static_cast<size_t>(count);
      if (headerBytes_ != sizeof(headerBuffer_)) continue;

      memcpy(&rxHeader_, headerBuffer_, sizeof(rxHeader_));
      if (rxHeader_.magic != WireProtocol::MAGIC ||
          rxHeader_.version != WireProtocol::VERSION ||
          rxHeader_.payloadLength > WireProtocol::MAX_RX_PAYLOAD) {
        Serial.println("Invalid TCP frame; closing client");
        client_.stop();
        resetReceive();
        return;
      }
    }

    const size_t wanted = rxHeader_.payloadLength - payloadBytes_;
    if (wanted > 0) {
      const int count = client_.read(received_.payload + payloadBytes_, wanted);
      if (count <= 0) return;
      payloadBytes_ += static_cast<size_t>(count);
      if (payloadBytes_ != rxHeader_.payloadLength) continue;
    }

    if (WireProtocol::crc32(received_.payload, payloadBytes_) != rxHeader_.payloadCrc32) {
      Serial.println("Dropping TCP frame with bad CRC32");
      resetReceive();
      return;
    }

    received_.type = rxHeader_.type;
    received_.flags = rxHeader_.flags;
    received_.sequence = rxHeader_.sequence;
    received_.payloadLength = payloadBytes_;
    pendingFrame_ = true;
  }
}

bool TransportService::popReceived(ReceivedFrame &frame) {
  if (!pendingFrame_) return false;
  frame = received_;
  pendingFrame_ = false;
  resetReceive();
  return true;
}

bool TransportService::sendFrame(WireProtocol::MessageType type, const void *payload,
                                 size_t length, uint16_t flags) {
  if (!connected() || (length > 0 && payload == nullptr)) return false;

  WireProtocol::FrameHeader header = {};
  header.magic = WireProtocol::MAGIC;
  header.version = WireProtocol::VERSION;
  header.type = type;
  header.flags = flags;
  header.sequence = txSequence_++;
  header.timestampUs = esp_timer_get_time();
  header.payloadLength = length;
  header.payloadCrc32 = WireProtocol::crc32(static_cast<const uint8_t *>(payload), length);

  return writeAll(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) &&
         (length == 0 || writeAll(static_cast<const uint8_t *>(payload), length));
}

bool TransportService::writeAll(const uint8_t *data, size_t length) {
  size_t sent = 0;
  while (sent < length && connected()) {
    const size_t count = client_.write(data + sent, length - sent);
    if (count == 0) return false;
    sent += count;
  }
  return sent == length;
}

void TransportService::resetReceive() {
  headerBytes_ = 0;
  payloadBytes_ = 0;
  memset(&rxHeader_, 0, sizeof(rxHeader_));
  if (!pendingFrame_) received_.payloadLength = 0;
}
