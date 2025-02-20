idf_component_register(SRCS "main.c" "file_server.c" "mount.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "favicon.ico" "upload_script.html"
                    REQUIRES fatfs spiffs vfs esp_http_server nvs_util wifi_util)
