#include "CommandHandler.h"

void CommandHandler::handle(const ReceivedFrame &frame, TransportService &transport) {
  if (frame.type != WireProtocol::MessageType::Command) {
    Serial.printf("Ignoring unsupported inbound message type 0x%02x\n",
                  static_cast<unsigned>(frame.type));
    return;
  }

  Serial.printf("Command #%lu (%u bytes): ", static_cast<unsigned long>(frame.sequence),
                static_cast<unsigned>(frame.payloadLength));
  Serial.write(frame.payload, frame.payloadLength);
  Serial.println();

  // The command body deliberately remains opaque until commands are defined.
  char acknowledgement[96];
  const int length = snprintf(acknowledgement, sizeof(acknowledgement),
                              "{\"status\":\"accepted\",\"rx_sequence\":%lu}",
                              static_cast<unsigned long>(frame.sequence));
  if (transport.sendFrame(WireProtocol::MessageType::CommandAck, acknowledgement,
                          length)) {
    Serial.printf("Command #%lu acknowledgement sent\n",
                  static_cast<unsigned long>(frame.sequence));
  } else {
    Serial.printf("Command #%lu acknowledgement failed: client disconnected\n",
                  static_cast<unsigned long>(frame.sequence));
  }
}
