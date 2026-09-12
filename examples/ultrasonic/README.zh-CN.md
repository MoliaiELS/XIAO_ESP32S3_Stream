# DYP-A22 UART 自动版独立测试

适用 DYP-A22YYUW-V1.0；依据工作区《DYP-RD-产品规格书(DYP-A22-V1.0)-230522-A0.pdf》3.1 节（印刷页5～6）、电气条件（页4）和寄存器0x0209（页13）。不适用于UART受控、PWM、开关量、IIC或RS485版本。

## 接线

断电接线，按接头编号/丝印确认，不根据未知线色猜测。

| 模块引线编号 | 模块功能 | XIAO ESP32S3 |
|---|---|---|
| 1 | VCC | 3V3（规格支持3.3～12V） |
| 2 | GND | GND |
| 3 | RX：输出选择 | 悬空，输出处理后的稳定值 |
| 4 | TX：串口输出 | D2 / GPIO3 |

UART自动版的RX是处理值/实时值选择引线，本例不要把它当普通接收脚接到ESP32 TX。若需实时值，断电后把模块RX接GND再上电；其电平只在上电500ms内采样。悬空/高为处理值，响应约300～500ms；低为实时值，约100～130ms。上电工作时间≤650ms。

本例使用3.3V供电。手册标注峰值电流150mA，不能只按平均9mA估算供电；多设备同时运行时应检查电源容量与压降。若改用5V等独立供电，需要共地，并确认TX仍为3.3V兼容逻辑，不能把5V信号直接接ESP32 GPIO。不要将独立电源输出与XIAO 3V3并接。

引脚分配：马达D4/GPIO5、D5/GPIO6；IMU D6/GPIO43、D7/GPIO44；超声波D2/GPIO3，D3/GPIO4暂不用。硬件串口分别为IMU UART1、超声波UART2，调试使用USB CDC Serial。

## 上传与测试

1. Arduino IDE打开本目录`ultrasonic.ino`，`ultrasonic.hpp`保持同目录。
2. 选择XIAO_ESP32S3，USB CDC On Boot=Enabled，选择端口上传；串口监视器115200。
3. 上电等至少650ms，用平整纸板正对探头，从约20～100cm移动观察。
4. 发`d`查看统计，`p`暂停/恢复刷屏，接收仍持续运行。

显示mm/cm、原始值；`NO_TARGET`表示未测到目标（0xFFFD），`INTERFERENCE`表示同频声波干扰（0xFFFE），不会把这两个值当距离。超过1500ms无有效帧，fresh=false，距离接口返回NAN，串口提示STALE。校验失败不会更新数据时间。

重要：示例**假设寄存器0x0209为0，即输出mm**；手册没有明确这个寄存器的默认值。若厂商配置为1（us），将初始化参数改为`UltrasonicParser::Unit::Microseconds`，按手册除以5.75转换mm。帧本身没有单位标记，程序不能自动判断。用已知距离核对；本例不修改模块配置。

bytes为0：检查接线、型号和启动等待；bytes增长但frames不增长：检查波特率（出厂115200，但可被修改）及checksum错误；NO_TARGET：检查目标距离、角度、大小；INTERFERENCE：暂时关闭附近其他超声波设备。标称平面量程1～300cm，实际受目标与安装影响。

## 接口

`Ultrasonic sensor(uart);`支持独立实例，每个设备需要独立UART及RX引脚，不可把多个TX并到同一RX。setup调用`ultrasonic_init(rxGPIO,unit,baud)`，loop持续调用`ultrasonic_update()`。检查`valid(millis())`后读取`distance_mm(millis())`；`fresh()`仅表示近期收到有效校验帧（包括未测到目标的状态帧）。

四字节协议：FF、距离高字节、低字节、前三字节累加和低8位。规格示例`FF 07 A1 A7`原始值1953：mm模式为1953mm，us模式约339.65mm。没有发送触发命令，也不在软件内切换硬件接口模式。
