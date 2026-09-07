#include "CameraService.h"

#include <cstring>

#include "Config.h"
#include "camera_pins.h"

namespace {
constexpr char STREAM_CONTENT_TYPE[] = "multipart/x-mixed-replace;boundary=frame";
constexpr char STREAM_BOUNDARY[] = "\r\n--frame\r\n";
constexpr char STREAM_PART[] = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
}  // namespace

bool CameraService::beginCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = AppConfig::CAMERA_FRAME_SIZE;
  config.jpeg_quality = AppConfig::CAMERA_JPEG_QUALITY;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = AppConfig::CAMERA_FRAME_BUFFERS;
  } else {
    Serial.println("Warning: PSRAM not found; falling back to QVGA/DRAM");
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
  }

  const esp_err_t error = esp_camera_init(&config);
  if (error != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", error);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr && sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, -2);
  }

  cameraReady_ = true;
  Serial.println("Camera initialized");
  return true;
}

bool CameraService::beginServer(uint16_t port) {
  if (!cameraReady_) return false;
  if (server_ != nullptr) return true;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
  config.ctrl_port = port + 32768;
  // ESP-IDF reserves three of these internally; leave room for browser clients.
  config.max_open_sockets = 5;
  config.lru_purge_enable = true;

  if (httpd_start(&server_, &config) != ESP_OK) return false;

  const httpd_uri_t rootUri = {
      .uri = "/", .method = HTTP_GET, .handler = rootHandler, .user_ctx = nullptr};
  const httpd_uri_t streamUri = {
      .uri = "/stream", .method = HTTP_GET, .handler = streamHandler, .user_ctx = nullptr};

  if (httpd_register_uri_handler(server_, &rootUri) != ESP_OK ||
      httpd_register_uri_handler(server_, &streamUri) != ESP_OK) {
    httpd_stop(server_);
    server_ = nullptr;
    return false;
  }
  return true;
}

esp_err_t CameraService::rootHandler(httpd_req_t *request) {
  static const char page[] =
      "<!doctype html><meta name='viewport' content='width=device-width'>"
      "<title>XIAO Wearable</title><style>body{margin:0;background:#111;color:#eee;"
      "font-family:sans-serif;text-align:center}img{max-width:100%;height:auto}</style>"
      "<h2>XIAO ESP32S3 Sense</h2><img src='/stream'>";
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t CameraService::streamHandler(httpd_req_t *request) {
  httpd_resp_set_type(request, STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");

  char partHeader[64];
  esp_err_t result = ESP_OK;
  while (result == ESP_OK) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr) {
      result = ESP_FAIL;
      break;
    }

    if (frame->format != PIXFORMAT_JPEG) {
      esp_camera_fb_return(frame);
      result = ESP_FAIL;
      break;
    }

    const int headerLength = snprintf(partHeader, sizeof(partHeader), STREAM_PART,
                                      static_cast<unsigned>(frame->len));
    result = httpd_resp_send_chunk(request, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(request, partHeader, headerLength);
    }
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(request,
                                     reinterpret_cast<const char *>(frame->buf), frame->len);
    }
    esp_camera_fb_return(frame);
    delay(1);
  }
  return result;
}
