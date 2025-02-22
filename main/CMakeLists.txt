idf_component_register(SRCS "main.c" "esp32s3_file_server.c" "mount.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "favicon.ico"
                    REQUIRES fatfs spiffs vfs esp_http_server json nvs_util wifi_util config_db)
