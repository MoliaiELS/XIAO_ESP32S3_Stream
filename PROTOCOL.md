# XIAO wearable wire protocol v2

The camera stream is HTTP MJPEG at `http://DEVICE_IP/stream`. Sensor data,
audio and laptop-to-device commands use one full-duplex TCP connection on port
`9000`. Only one TCP client is accepted at a time.

## Frame

Every integer and float is little-endian (ESP32 native byte order). A frame is a
28-byte packed header followed by `payload_length` bytes.

| Offset | Type | Field | Meaning |
| ---: | --- | --- | --- |
| 0 | `char[4]` | magic | ASCII `XIAO` |
| 4 | `u8` | version | `2` |
| 5 | `u8` | type | message type below |
| 6 | `u16` | flags | bit 0: more fragments; bit 1: error; bit 2: stream start; bit 3: stream end |
| 8 | `u32` | sequence | sender-local monotonically increasing number |
| 12 | `u64` | timestamp_us | device: microseconds since boot; laptop: its own clock |
| 20 | `u32` | payload_length | payload bytes; inbound maximum is 1024 |
| 24 | `u32` | payload_crc32 | standard CRC-32/ISO-HDLC of payload |

CRC32 covers only the payload. TCP is a byte stream: a receiver must accumulate
the complete header and payload rather than assume one `recv()` is one frame.

## Message types and payloads

| Type | Direction | Payload |
| --- | --- | --- |
| `0x10` IMU | device -> laptop | 11 `float32`: accel XYZ m/s², gyro XYZ rad/s, quaternion WXYZ, temperature °C |
| `0x11` TOF | device -> laptop | `<HHHBB`: distance mm, signal rate, ambient rate, range status, reserved |
| `0x12` audio PCM | device -> laptop | `<IIIHBB` prefix then PCM16: stream ID, chunk index, sample rate, sample count, channels, bits/sample |
| `0x13` status | device -> laptop | `<IIbBH`: uptime ms, free heap bytes, RSSI dBm, sensor flags, reserved |
| `0x20` command | laptop -> device | UTF-8 JSON object |
| `0x21` command ACK | device -> laptop | UTF-8 JSON object |

Recommended command envelope (content is intentionally not fixed yet):

```json
{"id":"host-0001","name":"play_audio","args":{"format":"pcm_s16le"}}
```

Large data such as future downlink speech should be split into bounded frames;
use `FLAG_MORE_FRAGMENTS` on every non-final fragment. The current firmware
accepts and acknowledges command frames but does not execute command names.

## Audio utterances

One button-triggered recording uses one `streamId`. Its first PCM frame has
`FLAG_STREAM_START | FLAG_MORE_FRAGMENTS`; intermediate frames have
`FLAG_MORE_FRAGMENTS`; a final zero-sample prefix has `FLAG_STREAM_END`.
`chunkIndex` starts at zero. PCM samples are signed 16-bit little-endian mono.
