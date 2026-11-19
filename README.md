# XIAO ESP32S3 Sense 可穿戴终端

本项目使用 XIAO ESP32S3 Sense 采集视频、IMU、超声波和麦克风数据，通过 Wi-Fi 发送给 laptop，并接收 laptop 下发的马达等控制命令。适用于盲人辅助可穿戴设备的端侧采集与执行。

## 当前功能

- HTTP MJPEG 视频流，可直接用浏览器查看。
- TCP 双向数据通道，传输 IMU、超声波、状态和 PCM 音频。
- BOOT 键控制录音；音频可实时发送，也可按配置保存到 microSD。
- DRV2605L + LRA 马达，支持远程触发震动。
- Wi-Fi 自动重连，串口输出 IP、传感器状态、录音和保存节点。
- USER_LED：Wi-Fi 断开时熄灭，连接后常亮，录音或保存时快速闪烁。

当前不会生成模拟传感器数据；没有收到合法硬件帧时不会上传伪造样本。

## 程序结构

| 文件 | 作用 |
| --- | --- |
| `XIAO_ESP32S3_Stream.ino` | 程序入口；初始化设备、维护 Wi-Fi 并调度各服务。 |
| `Config.h` | Wi-Fi、端口、摄像头、传感器、马达、音频、LED 和 SD 配置；包含引脚冲突检查。 |
| `CameraService.*` | 初始化 OV3660/OV5640，提供网页和 MJPEG 视频流。 |
| `ImuSensor.*` | UART1 IMU 驱动，解析四元数和 RPY。 |
| `UltrasonicSensor.*` | UART2 DYP-A22 自动版驱动。 |
| `MicrophoneSensor.*` | 板载 PDM 麦克风，输出 16 kHz PCM16 单声道音频。 |
| `HapticMotor.h` | I²C DRV2605L/LRA 驱动，负责震动、定时停止和故障检测。 |
| `SensorManager.*` | 轮询传感器，处理录音、LED、SD 保存和数据上传。 |
| `TransportProtocol.*` | 定义二进制帧、载荷结构和 CRC32。 |
| `TransportService.*` | TCP 服务端，负责收发、拆包和校验。 |
| `CommandHandler.*` | 解析 JSON 命令、执行马达动作并发送 ACK。 |
| `laptop_client.py` | laptop 参考客户端；显示传感器数据、保存接收到的 WAV 并发送命令。 |
| `PROTOCOL.md` | 完整通信协议。 |

## 接线

| 模块 | 通信方式 | XIAO 引脚 |
| --- | --- | --- |
| DRV2605L | I²C，地址 `0x5A` | D4/GPIO5 → SDA，D5/GPIO6 → SCL |
| IMU | UART1，115200 8N1 | D7/GPIO44 ← IMU TX；可选 D6/GPIO43 → IMU RX |
| DYP-A22 自动版 | UART2，115200 8N1 | D0/GPIO1 ← 模块 TX；模块 RX 悬空 |

所有模块必须共地。IMU 默认仅接收，因此 `IMU_TX_PIN=-1`。

### GPIO1 与 microSD

超声波接收改用 D0/GPIO1，microSD CS 继续使用 GPIO3，两者不再冲突。GPIO1 同时是 D0/A0，作为 UART 后不能再用于模拟量采集。默认配置仍不保存到 SD：

```cpp
constexpr bool ENABLE_ULTRASONIC = true;
constexpr bool ENABLE_SD_AUDIO_RECORDING = false;
```

这不会影响视频、传感器或音频通过 Wi-Fi 发送。需要本地录音时，可直接将 `ENABLE_SD_AUDIO_RECORDING` 改为 `true`。请确保 DYP-A22 TX 电平不超过 3.3V；如果是 5V，需要使用分压或电平转换。

## Arduino 配置

1. 安装 **esp32 by Espressif Systems 3.0.x**。
2. 选择开发板 `XIAO_ESP32S3`。
3. USB CDC On Boot 选择 **Enabled**；PSRAM 选择 `OPI PSRAM`，分区应提供至少 3 MB APP 空间。
4. 使用 OV5640 自动对焦时，安装 `OV5640 Auto Focus for ESP32 Camera` 0.1.1。
5. 打开 `Config.h` 填写 2.4 GHz Wi-Fi：

```cpp
constexpr char WIFI_SSID[] = "你的 Wi-Fi 名称";
constexpr char WIFI_PASSWORD[] = "你的 Wi-Fi 密码";
```

`Config.h` 会被 Git 跟踪，公开仓库前应移除真实密码及其 Git 历史。

上传后以 `115200` 波特率打开串口监视器。连接成功时会输出设备 IP、RSSI、视频地址和 TCP 地址，之后自动报告连接状态并在断线后重连。

## USB IMU 姿态网页

无需电脑连接开发板热点。上传根目录固件后，关闭串口监视器并运行：

```powershell
python -m pip install -r requirements-imu.txt
python imu_web.py --serial COM3
```

打开 http://127.0.0.1:8765 。接线必须是 **IMU TX → D7/GPIO44**。完整 USB/Wi-Fi 使用说明见 [IMU_USB.md](IMU_USB.md)。

## 使用

### 查看视频

```text
http://设备IP/
http://设备IP/stream
```

视频为 MJPEG，即连续 JPEG 图片，不是 H.264。

### 接收数据和音频

```powershell
python .\laptop_client.py 设备IP
```

保持客户端连接，短按 BOOT 开始录音，再按一次停止；达到 20 秒上限也会自动停止。默认不写 SD，laptop 会把收到的音频保存到 `recordings/`。

```cpp
constexpr bool ENABLE_AUDIO_TCP_STREAM = true;    // 发送到 laptop
constexpr bool ENABLE_SD_AUDIO_RECORDING = false; // 保存到 microSD
```

未启用 SD 时，IMU、超声波、状态、视频和在线录音发送均可正常工作。若开始录音时 laptop 未连接且 SD 未启用，录音不会启动。

### 马达命令

```powershell
python .\laptop_client.py 设备IP --command '{"id":"1","name":"motor_beep","args":{"preset":2}}'
python .\laptop_client.py 设备IP --command '{"id":"2","name":"motor_pulse","args":{"strength":80,"duration_ms":250}}'
python .\laptop_client.py 设备IP --command '{"id":"3","name":"motor_stop","args":{}}'
```

`preset` 为 1～3；`strength` 为 1～127；`duration_ms` 为 1～1000。还支持 `motor_status` 和对应的 `haptic_*` 别名。

## 通信协议摘要

视频独立使用 HTTP 端口 `80`。传感器、音频和命令共用全双工 TCP 端口 `9000`，当前只允许一个 laptop 客户端。

协议版本为 v3。每帧包含 28 字节小端序帧头和载荷：

```text
magic | version | type | flags | sequence | timestamp_us
      | payload_length | payload_crc32 | payload
```

| 类型 | 方向 | 载荷 |
| --- | --- | --- |
| `0x10` | ESP32 → laptop | IMU：`<7f>`，四元数 WXYZ + RPY 弧度 |
| `0x11` | ESP32 → laptop | 超声波：`<HHBBH>`，距离、原始值、状态、单位、保留值 |
| `0x12` | ESP32 → laptop | 音频：`<IIIHBB>` 前缀 + PCM16 数据 |
| `0x13` | ESP32 → laptop | 状态：`<IIbBH>`，运行时间、内存、RSSI、传感器标志 |
| `0x20` | laptop → ESP32 | UTF-8 JSON 命令 |
| `0x21` | ESP32 → laptop | UTF-8 JSON ACK |

详细字段和分片规则见 [PROTOCOL.md](PROTOCOL.md)。

## 注意事项

- XIAO ESP32S3 仅支持 2.4 GHz Wi-Fi，并需安装外置天线。
- BOOT 为 GPIO0 启动配置引脚，不要在复位或上电时持续按住。
- USER_LED 为 GPIO21、低电平点亮；若旧硬件实际使用 GPIO21 作为 SD CS，程序会禁用 LED。
- 摄像头、Wi-Fi、超声波和马达同时工作时需确认电源容量，并注意设备发热和佩戴安全。
- TCP 和 HTTP 当前均为明文，仅适合可信局域网。
