# bsp_motor.hpp：可实例化的 LRA 马达驱动

Arduino IDE 打开 **MotorApp.ino**，其中包含完整示例（马达实例、setup 和 loop），驱动完整包含在 **bsp_motor.hpp**。只需这两个文件放在 MotorApp 目录，不再依赖旧 MotorOnlyTest 的任何 .h。

默认启用一个马达，保持已成功测试的硬件设置：DRV2605L + LRA1040-0301、LRA 开环、200Hz、2.5V 峰值上限。上电不自动震动。

## 接线和运行

| XIAO | DRV2605L |
| --- | --- |
| D4 / GPIO5 | SDA |
| D5 / GPIO6 | SCL |
| 3V3 | VIN |
| GND | GND |
| 驱动板本地 GND | IN/TRIG |

马达接 OUT+、OUT−。两个输出都不接地。主程序初始化处明确注明：

```cpp
MotorBus motorBus(Wire);
BspMotor motor1(motorBus, "motor1");

// D4/GPIO5 -> SDA；D5/GPIO6 -> SCL
motor1.motor_init(D4, D5);
```

选择开发板 XIAO_ESP32S3、USB CDC On Boot=Enabled，上传 MotorApp.ino。串口监视器设为 115200，逐次发送字符：

| 字符 | 调用 / 行为 |
| --- | --- |
| 1 | motor1.motor_beep(1)：32/127 强度，150ms |
| 2 | motor1.motor_beep(2)：64/127 强度，300ms |
| 3 | motor1.motor_beep(3)：100/127 强度，300ms |
| s | motor1.motor_stop() |
| p | motor1.motor_debug()：打印状态和首次错误 |
| r | motor1.motor_init(D4,D5)：清除记录并重新初始化，不自动震动 |

串口换行可有可无。若一次发 `123`，程序按顺序立即处理，后一个命令会替换前一个脉冲；要感受三个完整短震，应分开发送并等上一下停止。

## API

```cpp
MotorBus bus(Wire);                   // 一条物理 I2C 总线对应一个 MotorBus
BspMotor motor(bus, "left");         // 每块 DRV2605L 对应一个 BspMotor

bool ok = motor.motor_init(D4, D5);   // SDA、SCL，参数是 Dn 常量或 GPIO 数字
motor.motor_beep(1);                 // 内置档位 1/2/3
motor.motor_beep(80, 250);            // 自定义强度 1..127、持续 1..1000ms
motor.motor_update();                // 必须在 loop() 中持续调用
motor.motor_stop();                  // 停止输出并进入待机
motor.motor_debug();                 // 打印实例状态，不清除故障记录
bool ready = motor.motor_ready();
bool active = motor.motor_active();
bool fault = motor.motor_fault();
```

可选初始化参数：`motor.motor_init(D4,D5,2.5f,200.0f)`，分别为峰值电压上限和频率；本封装允许 0.1–3.3V、125–300Hz，实际使用必须符合马达规格。2.5V 峰值不是 2.5Vrms，保留了之前成功联调使用的保守配置。该驱动针对芯片 ID=7 的 DRV2605L 和 LRA，不提供 ERM 模式。

motor_beep() 不等待震动结束。每个实例独立计时，因此 loop 必须轮询每个实例。不要在主循环 delay(1000) 或执行长时间阻塞工作，否则停止会延迟；I²C 调用自身也有超时。已在震动时重复 beep 会重置该实例的强度和计时，其他实例不受影响。震动过程中调用 init 会被拒绝，先 stop。

## 两个马达：两条独立 I²C 总线

**每个马达需要一块 DRV2605L。两块板的地址都是固定的 0x5A，不能直接并在同一组 SDA/SCL 上分别控制。** ESP32S3 有两个硬件 I²C 控制器，两个马达可以分别接 Wire 和 Wire1：

```cpp
#include <Arduino.h>
#include "bsp_motor.hpp"

MotorBus bus0(Wire);
MotorBus bus1(Wire1);
BspMotor motor1(bus0, "motor1");
BspMotor motor2(bus1, "motor2");

void setup() {
  Serial.begin(115200);
  motor1.motor_init(D4, D5); // 第1板 SDA=D4/GPIO5，SCL=D5/GPIO6
  motor2.motor_init(D0, D1); // 第2板 SDA=D0/GPIO1，SCL=D1/GPIO2
}
void loop() {
  motor1.motor_update();
  motor2.motor_update();
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '3') motor1.motor_beep(uint8_t(c-'0'));
    if (c >= '4' && c <= '6') motor2.motor_beep(uint8_t(c-'3'));
    if (c == 's') { motor1.motor_stop(); motor2.motor_stop(); }
  }
  delay(1);
}
```

要启用这个方案，用上面代码替换 MotorApp.ino 的内容。仅当 D0/D1 和第2个 I²C 控制器未被其他外设占用时使用；以后集成摄像头等功能时需重新核对控制器资源。不要通过创建多个 TwoWire(0) 或多个 MotorBus(Wire) 来模拟新总线。

## 三个或更多马达：TCA9548A

封装支持一条 I²C 总线上一个 TCA9548A、最多 8 个通道，每通道一块 DRV2605L。构造 MotorBus 时传入复用器地址（0x70–0x77），构造马达时指定通道（0–7）。

```cpp
MotorBus bus(Wire, 0x70);
BspMotor motor1(bus, "motor1", 0);
BspMotor motor2(bus, "motor2", 1);
BspMotor motor3(bus, "motor3", 2);

// setup 中：引脚都是 XIAO 到 TCA9548A 的上游 SDA/SCL
motor1.motor_init(D4, D5);
motor2.motor_init(D4, D5);
motor3.motor_init(D4, D5);

// 在需要震动时调用，硬件可同时播放，I2C 命令依次发送
motor1.motor_beep(1);
motor2.motor_beep(2);
motor3.motor_beep(3);

// loop 中必须持续调用
motor1.motor_update();
motor2.motor_update();
motor3.motor_update();
```

接线：XIAO D4/D5 → TCA9548A 上游 SDA/SCL；通道0 SD0/SC0 → 驱动1 SDA/SCL；通道1 SD1/SC1 → 驱动2；通道2依次类推。TCA9548A 使用 3.3V 供电，RESET 保持高电平、A0/A1/A2 按模块说明配置地址，0x70 通常为三位均低。核对模块自带上拉电阻与 RESET 电路。各器件共地、各驱动 VIN 保持 3.3V；不要在上游再并接一个同地址驱动。

每次寄存器访问都会先只选通目标通道，停止/状态轮询也会选通，不会因上次访问了另一个马达而写错对象。MotorBus 只初始化一次，后续实例必须使用同样引脚。重复占用同一通道会明确报错；不应跨对象绕过这个检查。

多马达同时震动需要计算总电流，必要时使用独立、足额的稳压 3.3V 驱动电源并与 XIAO 共地，不要将独立稳压输出与 XIAO 3V3 输出并联。实例数量不等于板载电源一定能带动的数量。

## 生命周期与调试

- bus、motor、实例名称字符串应具有整个程序生命周期，推荐像 MotorApp.ino 一样定义为全局对象。构造函数不访问硬件，setup 中初始化。
- 一个控制器只创建一个共享 MotorBus；不要在其他代码中重新调用 Wire.begin/end 改变它的引脚。对象禁止复制，不支持运行中删除/重建和释放通道。
- 所有调用从同一个 Arduino loop/task 执行，不从中断/多个任务并发访问。复用器通道选择和寄存器读写未加跨任务锁。
- 串口日志含实例名，例如 `[motor2] INIT/READY/BEEP/STOP`，便于确认控制对象。
- 初始化检查芯片 ID、参数并逐项读回配置；错误保留 `FIRST_ERROR`、寄存器地址、错误码、时间和捕获的 STATUS。
- `CHIP_PROTECTION` 的 code=1/2/3 对应过流/过温/两者；I2C_WRITE、I2C_READ_POINTER 中 code 是 Wire 错误码；I2C_SHORT_READ 的 code 是读到字节数；MUX_SELECT 是通道切换失败。
- 故障锁存后停止并拒绝后续 beep；p/debug 不清记录，r/init 清除并重试。先保存日志再重新初始化。
- `stop_confirmed=0` 表示停止写入未确认，不能只靠 active=false 判断马达已停，需断开驱动电源检查。
- 本封装输出 Serial 调试日志；旧 MotorOnlyTest 的 LED/全地址扫描留在原示例中，不占用封装用户的 GPIO21。

## 验证

2026-09-08：默认单马达示例在迁移到 .ino 前已通过 ESP32 3.3.11 / XIAO_ESP32S3 / USB CDC Enabled 编译，程序 303441 字节、静态 RAM 23112 字节。此次仅将示例入口整理为 MotorApp.ino，删除重复入口文件，未重新编译。下述主机测试此前全部通过。此前单马达测试程序已由用户确认实际震动成功，本次封装尚未重新上传硬件。

主机模拟测试 `tests/bsp_motor_test.cpp` 覆盖：双总线独立定时、回绕、重复地址/引脚冲突、保护状态保留、停止失败、显式恢复、三路复用器隔离及错误、配置读回失败与短读取。测试替代的是 Wire/时钟，不是硬件实测；两马达及 TCA9548A 方案尚未上板验证。

仓库根目录可运行：

```powershell
g++ -std=c++11 -Wall -Wextra -Werror -I tests/motor_fakes tests/bsp_motor_test.cpp -o build/bsp_motor_test.exe
./build/bsp_motor_test.exe
```

硬件依据：[ESP32S3 I²C 控制器](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2c.html)、[TCA9548A](https://www.ti.com/product/TCA9548A)、[DRV2605L](https://www.ti.com/lit/ds/symlink/drv2605l.pdf)。
