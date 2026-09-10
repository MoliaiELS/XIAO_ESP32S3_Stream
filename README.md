# XIAO ESP32S3 Sense 可穿戴数据终端

本项目基于 Arduino ESP32 开发板包 **3.0.x**，将 XIAO ESP32S3 Sense 作为可穿戴设备的数据采集和通信终端：持续向 laptop 发送摄像头画面及传感器数据，同时接收 laptop 下发的控制命令。

## 当前功能

- 通过 HTTP 端口 `80` 持续发送 MJPEG 视频流。
- 通过 TCP 端口 `9000` 双向传输带帧头和 CRC32 校验的数据。
- 定期发送设备运行时间、剩余内存、Wi-Fi 信号和传感器状态。
- 接收 UTF-8 JSON 命令，输出到串口并返回确认帧。
- 使用板载 BOOT 键触发 16 kHz、16-bit、单声道录音：可实时发送给 laptop，也可同时保存为 microSD 上的 WAV 文件。
- 已定义 IMU、TOF 数据格式及驱动接口。

IMU、TOF 目前仅为接口占位，不生成模拟数据；接入具体硬件后再填写对应采集代码。麦克风已能录制真实 PCM、通过 TCP 实时发送并按配置保存 WAV。下行命令目前只确认收到，不执行具体动作。

## 程序层级

```text
应用入口 XIAO_ESP32S3_Stream.ino
├─ 摄像头服务 CameraService
│  └─ HTTP MJPEG 视频流
├─ 传感器层 SensorManager
│  ├─ ImuSensor
│  ├─ TofSensor
│  └─ MicrophoneSensor
├─ 双向传输层 TransportService
│  ├─ 上传传感器、音频和状态帧
│  └─ 接收 laptop 命令帧
├─ 协议层 TransportProtocol
│  └─ 消息类型、载荷结构、序号、时间戳和 CRC32
└─ 命令层 CommandHandler
   └─ 命令分发入口和 ACK 回复
```

视频和设备数据使用两条独立连接。HTTP 服务负责摄像头流；Arduino 主循环负责 TCP 命令接收、传感器轮询和状态发送，避免视频连接阻塞命令通道。

## 文件说明

| 文件 | 层级与作用 |
| --- | --- |
| `XIAO_ESP32S3_Stream.ino` | 应用入口；初始化摄像头、传感器和 Wi-Fi，维护重连并调度各服务。 |
| `Config.h` | 集中配置 Wi-Fi、端口、视频参数、音频发送/保存开关、录音时长、LED 和 SD 引脚。 |
| `camera_pins.h` | XIAO ESP32S3 Sense 摄像头 GPIO 定义。 |
| `CameraService.h/.cpp` | 初始化摄像头，提供 `/` 预览页面和 `/stream` MJPEG 流。 |
| `TransportProtocol.h/.cpp` | 定义二进制帧、消息类型、传感器载荷及 CRC32 算法。 |
| `TransportService.h/.cpp` | TCP 服务端；处理连接、拆包、校验、收发序号和完整写入。只允许一个 laptop 客户端。 |
| `CommandHandler.h/.cpp` | 处理 `Command` 帧；当前打印 JSON，并返回 `CommandAck`。后续命令逻辑从这里扩展。 |
| `SensorManager.h/.cpp` | 轮询传感器，处理 BOOT 键、状态 LED、PCM 音频发送、PSRAM 缓存、SD 探测及 WAV 保存。 |
| `ImuSensor.h/.cpp` | IMU 驱动占位；输出加速度、角速度、四元数和温度。 |
| `TofSensor.h/.cpp` | TOF 驱动占位；输出距离、信号强度、环境强度和测距状态。 |
| `MicrophoneSensor.h/.cpp` | 使用 `ESP_I2S` 驱动 GPIO 41/42 的 PDM 麦克风，读取单声道 PCM16。 |
| `PROTOCOL.md` | TCP 二进制协议的完整字段、字节序和载荷说明。 |
| `laptop_client.py` | 无第三方依赖的参考客户端；接收数据和音频、生成 laptop 端 WAV，并可发送一次 JSON 命令。 |
| `.gitignore` | 忽略 Arduino 构建产物和编辑器临时文件。 |

## Arduino 配置与上传

1. 在开发板管理器安装 **esp32 by Espressif Systems 3.0.x**。
2. 在库管理器中安装 `OV5640 Auto Focus for ESP32 Camera`（版本 0.1.1）。
3. 选择开发板 `XIAO_ESP32S3`。
4. 将 PSRAM 设置为 `OPI PSRAM`，建议使用至少 3 MB APP 空间的分区。
5. 安装 Sense 扩展板、摄像头和包装内的 Wi-Fi 天线；SD 录音需插入不大于 32 GB、FAT32 格式的 microSD 卡。
6. 按下一节配置 Wi-Fi。
7. 编译并上传 `XIAO_ESP32S3_Stream.ino`，以 `115200` 波特率打开串口监视器。

## Wi-Fi 设置

设备使用 **STA 模式**加入现有无线网络，不会自行创建热点。XIAO ESP32S3 只支持 2.4 GHz Wi-Fi；laptop 应接入同一个局域网。

1. 确认板载 U.FL 接口已经安装外置 Wi-Fi 天线。
2. 打开项目根目录中的 `Config.h`，填写网络名称和密码：

```cpp
constexpr char WIFI_SSID[] = "你的 2.4GHz Wi-Fi 名称";
constexpr char WIFI_PASSWORD[] = "你的 Wi-Fi 密码";
```

`Config.h` 会被 Git 跟踪。如果仓库可能公开或分享，请勿提交真实密码；已提交的密码还需要从 Git 历史中清理。

3. 上传程序并打开串口监视器。连接成功后会显示类似信息：

```text
[WiFi] Connected
[WiFi] SSID=YourWiFi, IP=192.168.1.123, RSSI=-45 dBm, channel=6
[WiFi] Video: http://192.168.1.123:80/stream
[WiFi] Data/commands/audio: 192.168.1.123:9000 (TCP)
```

连接时还会打印网关、子网掩码、DNS 和 MAC 地址；之后每 15 秒打印一次 IP、RSSI 和信道。设备掉线后每隔 5 秒自动尝试重连。若始终无法连接，请检查网络是否为 2.4 GHz、SSID/密码是否正确、天线是否装好，以及路由器是否启用了客户端隔离。

## 使用方式

浏览器访问：

```text
http://设备IP/
http://设备IP/stream
```

laptop 接收状态、传感器和音频帧：

```powershell
python .\laptop_client.py 设备IP
```

客户端默认把收到的每段语音保存到项目的 `recordings/` 目录。保持客户端连接后短按板载 BOOT 键开始录音，再按一次或达到 20 秒上限后停止。ESP32 每 20 ms 发送一个 PCM 块，最后发送结束帧；如果 TCP 中断但 SD 可用，本地录音仍会继续。

在 `Config.h` 中选择录音目的地：

```cpp
constexpr bool ENABLE_AUDIO_TCP_STREAM = true;   // 实时发送到 laptop
constexpr bool ENABLE_SD_AUDIO_RECORDING = true; // 同时保存到本地 microSD
```

两项都为 `true` 时同时发送和保存。只开 SD 时可完全离线录音；只开 TCP 时不挂载 SD，也不占用约 640 KB 的录音缓存。若启用的目的地当前都不可用，按键后串口会说明无法开始的原因。

USER_LED 状态如下：

| 设备状态 | LED |
| --- | --- |
| Wi-Fi 未连接，且未录音 | 熄灭 |
| Wi-Fi 已连接 | 常亮 |
| 正在录音或写入 SD | 约 100 ms 翻转一次，快速闪烁 |

依据 [Seeed XIAO ESP32S3 入门文档](https://wiki.seeedstudio.com/cn/xiao_esp32s3_getting_started/)，USER_LED 是 GPIO21，低电平点亮。若旧版硬件/接线实际将 SD 的 CS 放在 GPIO21，程序会优先保证 SD 数据安全，禁用 LED 并打印冲突提示。

BOOT 键连接 GPIO0。GPIO0 是启动配置引脚，因此不要在复位或上电时按住它；系统正常运行后可以短按。更换外接大按键时，修改 `Config.h` 中的 `VOICE_BUTTON_PIN`，按键另一端接 GND，并保持低电平有效。

### SD 录音实机验证记录

- 已实际录制并成功播放两条 WAV 文件。
- SPI 固定为 SCK GPIO7、MISO GPIO8、MOSI GPIO9。
- 初始化优先尝试 CS GPIO3，并兼容旧示例的 CS GPIO21；每个 CS 依次尝试 4 MHz 和 1 MHz，串口会打印实际成功配置。
- USER_LED 为 GPIO21、低电平点亮。实际 SD CS 若也是 GPIO21，程序会禁用 LED，避免灯光控制干扰存储卡。
- 当前单次最长 20 秒，约占用 640 KB PSRAM；修改 `AUDIO_MAX_RECORDING_MS` 时需同步考虑可用 PSRAM。

发送一条测试命令并继续接收数据：

```powershell
python .\laptop_client.py 设备IP --command '{"id":"1","name":"ping","args":{}}'
```

TCP 帧头固定为 28 字节、小端序，详细格式见 [PROTOCOL.md](PROTOCOL.md)。主要消息类型如下：

| 类型 | 方向 | 内容 |
| --- | --- | --- |
| `0x10` | 设备 → laptop | IMU |
| `0x11` | 设备 → laptop | TOF |
| `0x12` | 设备 → laptop | PCM 音频块 |
| `0x13` | 设备 → laptop | 设备状态 |
| `0x20` | laptop → 设备 | JSON 命令 |
| `0x21` | 设备 → laptop | 命令确认 |

## 后续扩展位置

- 接入具体 IMU：实现 `ImuSensor::begin()` 和 `ImuSensor::read()`。
- 接入具体 TOF：实现 `TofSensor::begin()` 和 `TofSensor::read()`。
- 调整本地录音：修改 `Config.h` 中的按钮 GPIO、录音时长、SD 引脚和 SPI 频率。
- 增加命令：在 `CommandHandler::handle()` 中解析 `name` 和 `args` 并分发；语音等大数据应使用分片标志发送。
- 调整画质或功耗：修改 `Config.h` 中的分辨率、JPEG 质量、帧缓冲数量和 `WIFI_MAX_THROUGHPUT`。

## 使用限制

- 视频为连续 JPEG 帧，不是 H.264；浏览器和 OpenCV 可直接读取。
- 当前 TCP 与 HTTP 均为明文，适合可信局域网开发环境；正式使用应增加身份认证和加密。
- `WIFI_MAX_THROUGHPUT=true` 可减少视频抖动，但会增加耗电与发热。用于可穿戴设备时应根据延迟和续航需求调整。
- 长时间视频传输会使 ESP32S3 发热，应注意供电、散热和佩戴安全。
