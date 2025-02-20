#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWD_MAX_LEN 32

wifi_mode_t query_wifi_mode(void);
esp_err_t save_wifi_mode(wifi_mode_t mode);

esp_err_t query_wifi_ssid(char* ssid, size_t len);
esp_err_t save_wifi_ssid(const char* ssid);

esp_err_t query_wifi_passwd(char* passwd, size_t len);
esp_err_t save_wifi_passwd(const char* passwd);
