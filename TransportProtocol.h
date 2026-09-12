#pragma once

#include <Arduino.h>

namespace WireProtocol {
constexpr uint32_t MAGIC = 0x4F414958;  // bytes on wire: "XIAO"
constexpr uint8_t VERSION = 3;
constexpr size_t MAX_RX_PAYLOAD = 1024;

enum class MessageType : uint8_t {
  Imu = 0x10,
  Ultrasonic = 0x11,
  AudioPcm = 0x12,
  Status = 0x13,
  Command = 0x20,
  CommandAck = 0x21,
};

enum FrameFlags : uint16_t {
  FLAG_NONE = 0,
  FLAG_MORE_FRAGMENTS = 1 << 0,
  FLAG_ERROR = 1 << 1,
  FLAG_STREAM_START = 1 << 2,
  FLAG_STREAM_END = 1 << 3,
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
  float quaternionWxyz[4];
  float rpyRad[3];
};

struct UltrasonicPayload {
  uint16_t distanceMm;
  uint16_t rawValue;
  uint8_t status;  // 0 waiting, 1 distance, 2 no target, 3 interference, 4 invalid
  uint8_t unit;    // 0 millimetres, 1 microseconds (converted to distanceMm)
  uint16_t reserved;
};

// Immediately followed by sampleCount * channels signed little-endian samples.
struct AudioPcmPrefix {
  uint32_t streamId;
  uint32_t chunkIndex;
  uint32_t sampleRateHz;
  uint16_t sampleCount;
  uint8_t channels;
  uint8_t bitsPerSample;
};

struct StatusPayload {
  uint32_t uptimeMs;
  uint32_t freeHeapBytes;
  int8_t wifiRssiDbm;
  uint8_t sensorFlags;  // bit0 IMU, bit1 ultrasonic, bit2 microphone, bit3 haptic
  uint16_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 28, "Unexpected protocol header layout");
static_assert(sizeof(ImuPayload) == 28, "Unexpected IMU payload layout");
static_assert(sizeof(UltrasonicPayload) == 8,
              "Unexpected ultrasonic payload layout");
static_assert(sizeof(AudioPcmPrefix) == 16, "Unexpected audio prefix layout");

uint32_t crc32(const uint8_t *data, size_t length);
}  // namespace WireProtocol
