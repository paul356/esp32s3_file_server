#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"

typedef enum {
    FILE_OPERATION_DOWNLOAD,
    FILE_OPERATION_UPLOAD,
    FILE_OPERATION_DELETE,
    FILE_OPERATION_IDLE
} file_operation_e;

esp_err_t turn_off_backlight(void);
esp_err_t display_init(void);
void update_service_uri(esp_ip4_addr_t new_addr);
void update_file_server_status(const char* file_path, file_operation_e op);