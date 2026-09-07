#pragma once

#include <Arduino.h>

namespace WireProtocol {
constexpr uint32_t MAGIC = 0x4F414958;  // bytes on wire: "XIAO"
constexpr uint8_t VERSION = 1;
constexpr size_t MAX_RX_PAYLOAD = 1024;

enum class MessageType : uint8_t {
  Imu = 0x10,
  Tof = 0x11,
  AudioPcm = 0x12,
  Status = 0x13,
  Command = 0x20,
  CommandAck = 0x21,
};

enum FrameFlags : uint16_t {
  FLAG_NONE = 0,
  FLAG_MORE_FRAGMENTS = 1 << 0,
  FLAG_ERROR = 1 << 1,
};

#pragma pack(push, 1)
struct FrameHeader {
  uint32_t magic;
  uint8_t version;
  MessageType type;
  uint16_t flags;
  uint32_t sequence;
  uint64_t timestampUs;
  uint32_t payloadLength;
  uint32_t payloadCrc32;
};

struct ImuPayload {
  float accelMps2[3];
  float gyroRadps[3];
  float quaternionWxyz[4];
  float temperatureC;
};

struct TofPayload {
  uint16_t distanceMm;
  uint16_t signalRate;
  uint16_t ambientRate;
  uint8_t rangeStatus;
  uint8_t reserved;
};

// Immediately followed by sampleCount * channels signed little-endian samples.
struct AudioPcmPrefix {
  uint32_t sampleRateHz;
  uint16_t sampleCount;
  uint8_t channels;
  uint8_t bitsPerSample;
};

struct StatusPayload {
  uint32_t uptimeMs;
  uint32_t freeHeapBytes;
  int8_t wifiRssiDbm;
  uint8_t sensorFlags;  // bit0 IMU, bit1 TOF, bit2 microphone
  uint16_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 28, "Unexpected protocol header layout");

uint32_t crc32(const uint8_t *data, size_t length);
}  // namespace WireProtocol
