#pragma once

#include <Arduino.h>
#include "esp_camera.h"

namespace AppConfig
{
    // Wi-Fi credentials. This file is tracked by Git; do not commit real passwords
    // if the repository may be shared or published.
    constexpr char WIFI_SSID[] = "moliai";
    constexpr char WIFI_PASSWORD[] = "0o4z22vl";

    constexpr char HOSTNAME[] = "xiao-wearable";
    constexpr uint16_t VIDEO_PORT = 80;
    constexpr uint16_t DATA_PORT = 9000;
    constexpr uint32_t WIFI_RETRY_MS = 5000;

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

    constexpr uint32_t AUDIO_SAMPLE_RATE_HZ = 16000;
    constexpr uint16_t AUDIO_CHUNK_MS = 20;
    constexpr uint16_t AUDIO_VAD_RMS_THRESHOLD = 700;
    constexpr uint32_t AUDIO_WAIT_FOR_SPEECH_MS = 3000;
    constexpr uint32_t AUDIO_SILENCE_TIMEOUT_MS = 1000;
    constexpr uint32_t AUDIO_MAX_RECORDING_MS = 15000;

    constexpr uint32_t STATUS_INTERVAL_MS = 5000;
} // namespace AppConfig
