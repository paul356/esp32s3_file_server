idf_component_register(SRCS "display.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd lvgl esp_driver_gpio wifi_util)
