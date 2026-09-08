#pragma once
#include <stdint.h>

namespace HardwareConfig {
constexpr int SDA_PIN = 5;  // XIAO D4 / GPIO5, NOT GPIO4
constexpr int SCL_PIN = 6;  // XIAO D5 / GPIO6
enum class MotorType { Unknown, ERM, LRA };
constexpr MotorType MOTOR_TYPE = MotorType::LRA;
// LRA1040-0301: supplied sheet specifies 2.5-3.7 Vrms, 180-220 Hz.
// Conservative initial test: 2.5 V PEAK ceiling, not 2.5 Vrms.
constexpr float MOTOR_VOLTAGE_LIMIT = 2.5f;
constexpr float LRA_FREQUENCY_HZ = 200.0f;
constexpr uint32_t MAX_PULSE_MS = 1000;
}
