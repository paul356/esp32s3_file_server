#pragma once

#include "esp_err.h"

esp_err_t display_init(void);
esp_err_t display_show_message(const char* message);
