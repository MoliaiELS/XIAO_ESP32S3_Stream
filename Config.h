#pragma once

#include <Arduino.h>
#include "esp_camera.h"

namespace AppConfig {
// Wi-Fi credentials. This file is tracked by Git; do not commit real passwords
// if the repository may be shared or published.
constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

constexpr char HOSTNAME[] = "xiao-wearable";
constexpr uint16_t VIDEO_PORT = 80;
constexpr uint16_t DATA_PORT = 9000;
constexpr uint32_t WIFI_RETRY_MS = 5000;

// Disabling Wi-Fi sleep makes MJPEG steadier but consumes more battery.
constexpr bool WIFI_MAX_THROUGHPUT = true;

constexpr framesize_t CAMERA_FRAME_SIZE = FRAMESIZE_VGA;  // 640 x 480
constexpr uint8_t CAMERA_JPEG_QUALITY = 12;                // lower is better
constexpr uint8_t CAMERA_FRAME_BUFFERS = 2;

constexpr uint32_t STATUS_INTERVAL_MS = 5000;
}  // namespace AppConfig
