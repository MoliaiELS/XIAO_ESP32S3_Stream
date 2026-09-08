#pragma once
#include <stdint.h>

namespace HardwareConfig {
constexpr int IMU_RX = 44;  // XIAO D7 <- RS485 receiver RO
constexpr int IMU_TX = 43;  // XIAO D6; unused in receive-only wiring
constexpr uint32_t IMU_BAUD = 115200;
constexpr uint8_t IMU_ID = 1;
constexpr int SDA_PIN = 5;  // D4
constexpr int SCL_PIN = 6;  // D5
constexpr uint32_t STALE_MS = 250;

enum class MotorType { Unknown, ERM, LRA };
// Fill these from the MOTOR specification, not the driver-board part number.
constexpr MotorType MOTOR_TYPE = MotorType::LRA;  // User: LRA1040-0301, Z axis
// Conservative drive ceiling: <= motor rated voltage AND <= 3.3 V.
// For LRA this is a PEAK ceiling (do not multiply the RMS rating by sqrt(2)).
constexpr float MOTOR_VOLTAGE_LIMIT = 2.5f;  // conservative PEAK, below 2.5 Vrms rating
constexpr float LRA_FREQUENCY_HZ = 200.0f;  // initial midpoint of supplied 180-220 Hz range
constexpr uint32_t MAX_PULSE_MS = 1000;
}  // namespace HardwareConfig
