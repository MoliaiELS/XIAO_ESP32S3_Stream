#pragma once

#include <Arduino.h>
#include "esp_camera.h"

namespace AppConfig
{
    // A BOOT-button recording can be sent to the connected laptop, saved to
    // microSD, or both. Network sending requires laptop_client.py to be connected.
    constexpr bool ENABLE_AUDIO_TCP_STREAM = true;   // 是否发送到电脑
    constexpr bool ENABLE_SD_AUDIO_RECORDING = true; // 是否本地保存

    // Wi-Fi credentials. This file is tracked by Git; do not commit real passwords
    // if the repository may be shared or published.
    constexpr char WIFI_SSID[] = "moliai";
    constexpr char WIFI_PASSWORD[] = "0o4z22vl";

    constexpr char HOSTNAME[] = "xiao-wearable";
    constexpr uint16_t VIDEO_PORT = 80;
    constexpr uint16_t DATA_PORT = 9000;
    constexpr uint32_t WIFI_RETRY_MS = 5000;
    constexpr uint32_t WIFI_STATUS_INTERVAL_MS = 15000;

    // Disabling Wi-Fi sleep makes MJPEG steadier but consumes more battery.
    constexpr bool WIFI_MAX_THROUGHPUT = true;

    constexpr framesize_t CAMERA_FRAME_SIZE = FRAMESIZE_VGA; // 640 x 480
    constexpr uint8_t CAMERA_JPEG_QUALITY = 12;              // lower is better
    constexpr uint8_t CAMERA_FRAME_BUFFERS = 2;
    // Method 1 from the Seeed Wiki. Used only when an OV5640 is detected.
    constexpr bool CAMERA_ENABLE_OV5640_AUTOFOCUS = true; // OV5640 自动对焦

    // Voice input: the onboard BOOT key is GPIO0 and active low. GPIO0 is a
    // strapping pin, so do not hold the key while resetting or powering on.
    constexpr uint8_t VOICE_BUTTON_PIN = 0;
    constexpr bool VOICE_BUTTON_ACTIVE_LOW = true;
    constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;

    // Arduino's XIAO ESP32S3 variant maps LED_BUILTIN to the active-low
    // orange USER_LED on GPIO21.
    constexpr uint8_t RECORDING_LED_PIN = LED_BUILTIN;
    constexpr bool RECORDING_LED_ACTIVE_LOW = true;
    constexpr uint32_t LED_FAST_BLINK_MS = 100;

    constexpr uint32_t AUDIO_SAMPLE_RATE_HZ = 16000;
    constexpr uint16_t AUDIO_CHUNK_MS = 20;
    // When SD saving is enabled, audio is buffered in PSRAM and written after
    // recording stops. 20 seconds of PCM16 mono at 16 kHz uses about 640 KB.
    constexpr uint32_t AUDIO_MAX_RECORDING_MS = 20UL * 1000UL;

    // Verified Sense SD bus. Current pin tables use CS=GPIO3; older examples
    // use GPIO21, so initialization probes both at 4 MHz then 1 MHz.
    constexpr uint8_t SD_SCK_PIN = 7;
    constexpr uint8_t SD_MISO_PIN = 8;
    constexpr uint8_t SD_MOSI_PIN = 9;
    constexpr uint8_t SD_CS_PIN = 3;
    constexpr uint8_t SD_FALLBACK_CS_PIN = 21;
    constexpr uint32_t SD_SPI_FREQUENCY_HZ = 4000000;

    constexpr uint32_t STATUS_INTERVAL_MS = 5000;
} // namespace AppConfig
