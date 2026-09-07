# XIAO ESP32S3 Sense wearable stream

This Arduino project provides two independent Wi-Fi paths:

- HTTP MJPEG camera stream on port 80 (`/stream`)
- Full-duplex framed TCP on port 9000 for IMU, TOF, PCM audio, status and commands

The camera and status path work now. IMU, TOF and microphone source files are
deliberate non-faking placeholders until the actual sensor models and sampling
requirements are selected. Incoming JSON commands are validated at the frame
level, printed to Serial and acknowledged, but are not executed yet.

## Arduino setup

1. Install **esp32 by Espressif Systems 3.0.x** in Boards Manager.
2. Select **XIAO_ESP32S3** and enable **OPI PSRAM**. A partition with at least a
   3 MB application area is recommended.
3. Copy `secrets.example.h` to `secrets.h`, then set the 2.4 GHz Wi-Fi SSID and
   password. A placeholder local `secrets.h` is already present in this working
   copy and is excluded from Git.
4. Fit the supplied external Wi-Fi antenna and the Sense camera board.
5. Compile/upload `XIAO_ESP32S3_Stream.ino`, open Serial Monitor at 115200, and
   note the assigned IP address.

Open either of these URLs:

```text
http://DEVICE_IP/
http://DEVICE_IP/stream
```

Connect the included laptop client in another terminal:

```powershell
python .\laptop_client.py DEVICE_IP
python .\laptop_client.py DEVICE_IP --command '{"id":"1","name":"ping","args":{}}'
```

The exact binary format and extension rules are in [PROTOCOL.md](PROTOCOL.md).

## Project layout

- `CameraService.*`: camera initialization and continuous MJPEG server
- `TransportProtocol.*`: versioned little-endian binary format and CRC32
- `TransportService.*`: one-client TCP server, framing and reconnect handling
- `CommandHandler.*`: future command dispatch point
- `SensorManager.*`: non-blocking sensor publisher and device status
- `ImuSensor.*`, `TofSensor.*`, `MicrophoneSensor.*`: hardware integration points
- `laptop_client.py`: dependency-free reference receiver/command sender

## Wearable-device notes

`WIFI_MAX_THROUGHPUT` is enabled because Wi-Fi power saving often makes MJPEG
jittery. It costs battery and creates more heat; switch it off in `Config.h` if
latency is less important. VGA/JPEG quality 12 is a practical initial setting.
Resolution, quality and framebuffer count are centralized in `Config.h`.

The ESP32-S3 is not encoding H.264 here. The stream is a sequence of JPEG frames,
which keeps firmware simple and lets browsers/OpenCV consume it directly. For
production wearables, add authentication/encryption or use a trusted isolated LAN;
the initial protocol is intentionally plaintext for development.
