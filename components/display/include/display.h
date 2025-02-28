#pragma once

#include "esp_err.h"

typedef enum {
    FILE_OPERATION_DOWNLOAD,
    FILE_OPERATION_UPLOAD,
    FILE_OPERATION_DELETE,
    FILE_OPERATION_IDLE
} file_operation_e;

esp_err_t display_init(void);
void update_file_server_status(const char* file_path, file_operation_e op);