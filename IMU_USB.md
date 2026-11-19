# 本地 IMU 姿态网页

推荐 USB 模式：电脑保持原有互联网连接，不必加入开发板的 Wi-Fi。

## 接线与固件

断电接线：**IMU TX → XIAO D7/GPIO44**，GND 共地，模块按规格供电。不要把 D7 接到 IMU RX；当前接收模式不需要连接模块 RX。

上传仓库根目录 `XIAO_ESP32S3_Stream.ino`。Arduino IDE 选择 XIAO_ESP32S3、USB CDC On Boot=Enabled、PSRAM=OPI PSRAM。诊断时选择 Core Debug Level=Info。主程序会持续读取 IMU，并在 USB 串口以最多 10 Hz 输出新四元数，不依赖 Wi-Fi 或 TCP 客户端。

验证过的命令行板卡配置（ESP32 Arduino 3.3.11）：

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,USBMode=hwcdc,CDCOnBoot=default,DebugLevel=info .
```

该版本的 `CDCOnBoot=default` 表示 Enabled；`CDCOnBoot=cdc` 表示 Disabled。

## 新克隆后启动

在仓库根目录运行：

```powershell
python -m pip install -r requirements-imu.txt
python imu_web.py --serial COM3
```

打开 http://127.0.0.1:8765 。使用实际 USB 串口替换 COM3。关闭 Arduino 串口监视器，避免占用同一串口。Ctrl+C 停止服务。若 8765 被占用，可加 `--port 8766`。

如不想修改当前 Python 环境，可将依赖安装到项目内，程序会自动发现：

```powershell
python -m pip install --target build/serial-deps -r requirements-imu.txt
```

## Wi-Fi 模式

电脑与开发板网络互通时可运行：

```powershell
python imu_web.py --host 开发板IP
```

这里使用串口日志中的开发板 IP，不是电脑 IP。TCP 默认端口 9000，可用 `--device-port` 修改。支持整合固件 v3 的 28 字节四元数＋RPY，以及旧 v2 的 44 字节 IMU 载荷。设备只允许一个 TCP 客户端，不要同时运行 laptop_client.py。TCP 模式也接收录音到 recordings；USB 模式只读取姿态文本。

## 显示与检查

页面显示机体三维姿态、Roll/Pitch/Yaw、原始归一化四元数和样本年龄。四元数采用 w/x/y/z，右手系、机体到世界旋转；角度采用 ZYX 欧拉角。实际安装方向需通过转动模块核对。置零仅改变网页显示，不向 IMU 发送命令。超过 700 ms 无新样本会标为过期并保留最后姿态。

`bytes=0` 表示 ESP32 没有读到 IMU UART 字节；检查 TX→D7、供电和共地。计数增长但没有有效帧时检查波特率、协议和错误计数。网页 connected=true 只表示源连接打开，fresh=true 才表示近期收到有效姿态。

```powershell
python -m unittest test_imu_web -v
```

页面无 CDN 依赖；本地服务只监听 127.0.0.1。
