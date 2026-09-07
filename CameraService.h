#pragma once

#include <Arduino.h>
#include "esp_camera.h"
#include "esp_http_server.h"

class CameraService {
 public:
  bool beginCamera();
  bool beginServer(uint16_t port);
  bool ready() const { return cameraReady_; }

 private:
  static esp_err_t rootHandler(httpd_req_t *request);
  static esp_err_t streamHandler(httpd_req_t *request);

  bool cameraReady_ = false;
  httpd_handle_t server_ = nullptr;
};
