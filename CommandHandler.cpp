#include "CommandHandler.h"

namespace {
const char *jsonValue(const char *json, const char *key) {
  char pattern[40];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *position = strstr(json, pattern);
  if (!position) return nullptr;
  position = strchr(position + strlen(pattern), ':');
  if (!position) return nullptr;
  do {
    ++position;
  } while (*position == ' ' || *position == '\t' || *position == '\r' ||
           *position == '\n');
  return position;
}

bool jsonString(const char *json, const char *key, char *output,
                size_t outputSize) {
  const char *value = jsonValue(json, key);
  if (!value || *value != '\"' || outputSize == 0) return false;
  ++value;
  size_t used = 0;
  while (*value && *value != '\"') {
    if (*value == '\\' || used + 1 >= outputSize) return false;
    output[used++] = *value++;
  }
  if (*value != '\"') return false;
  output[used] = '\0';
  return true;
}

bool jsonUnsigned(const char *json, const char *key, uint32_t &output) {
  const char *value = jsonValue(json, key);
  if (!value || *value < '0' || *value > '9') return false;
  uint32_t result = 0;
  while (*value >= '0' && *value <= '9') {
    const uint8_t digit = uint8_t(*value++ - '0');
    if (result > (UINT32_MAX - digit) / 10) return false;
    result = result * 10 + digit;
  }
  output = result;
  return true;
}
}  // namespace

void CommandHandler::handle(const ReceivedFrame &frame,
                            TransportService &transport,
                            SensorManager &sensors) {
  if (frame.type != WireProtocol::MessageType::Command) {
    Serial.printf("Ignoring unsupported inbound message type 0x%02x\n",
                  static_cast<unsigned>(frame.type));
    return;
  }

  Serial.printf("Command #%lu (%u bytes): ",
                static_cast<unsigned long>(frame.sequence),
                static_cast<unsigned>(frame.payloadLength));
  Serial.write(frame.payload, frame.payloadLength);
  Serial.println();

  char json[WireProtocol::MAX_RX_PAYLOAD + 1];
  memcpy(json, frame.payload, frame.payloadLength);
  json[frame.payloadLength] = '\0';

  char name[32] = {};
  const bool hasName = jsonString(json, "name", name, sizeof(name));
  const char *status = "accepted";
  if (hasName && (!strcmp(name, "motor_beep") ||
                  !strcmp(name, "haptic_beep"))) {
    uint32_t preset = 0;
    status = jsonUnsigned(json, "preset", preset) && preset >= 1 && preset <= 3
                 ? (sensors.hapticBeep(uint8_t(preset)) ? "ok" : "failed")
                 : "invalid_args";
  } else if (hasName && (!strcmp(name, "motor_pulse") ||
                         !strcmp(name, "haptic_pulse"))) {
    uint32_t strength = 0, durationMs = 0;
    status = jsonUnsigned(json, "strength", strength) &&
                     jsonUnsigned(json, "duration_ms", durationMs) &&
                     strength >= 1 && strength <= 127 && durationMs >= 1 &&
                     durationMs <= 1000
                 ? (sensors.hapticPulse(uint8_t(strength), durationMs) ? "ok"
                                                                      : "failed")
                 : "invalid_args";
  } else if (hasName && (!strcmp(name, "motor_stop") ||
                         !strcmp(name, "haptic_stop"))) {
    status = sensors.hapticStop() ? "ok" : "failed";
  } else if (hasName && (!strcmp(name, "motor_status") ||
                         !strcmp(name, "haptic_status"))) {
    sensors.hapticDebug();
    status = "ok";
  }

  char acknowledgement[160];
  int length = snprintf(
      acknowledgement, sizeof(acknowledgement),
      "{\"status\":\"%s\",\"rx_sequence\":%lu,\"motor_ready\":%s,"
      "\"motor_active\":%s,\"motor_fault\":%s}",
      status, static_cast<unsigned long>(frame.sequence),
      sensors.hapticReady() ? "true" : "false",
      sensors.hapticActive() ? "true" : "false",
      sensors.hapticFault() ? "true" : "false");
  if (length < 0) return;
  if (size_t(length) >= sizeof(acknowledgement)) {
    length = sizeof(acknowledgement) - 1;
  }
  if (transport.sendFrame(WireProtocol::MessageType::CommandAck, acknowledgement,
                          length)) {
    Serial.printf("Command #%lu acknowledgement sent\n",
                  static_cast<unsigned long>(frame.sequence));
  } else {
    Serial.printf("Command #%lu acknowledgement failed: client disconnected\n",
                  static_cast<unsigned long>(frame.sequence));
  }
}
