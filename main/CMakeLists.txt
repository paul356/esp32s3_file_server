idf_component_register(SRCS "main.c" "esp32s3_file_server.c" "mount_sdcard.c"
                    INCLUDE_DIRS "."
                    REQUIRES fatfs spiffs vfs esp_http_server json nvs_util wifi_util display config_db)
# Create spiffs image of output files of vue build
spiffs_create_partition_image(storage ../web/dist/ FLASH_IN_PROJECT)
