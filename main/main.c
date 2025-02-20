/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* HTTP File Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "file_serving_example_common.h"
#include "config_db.h"
#include "wifi_intf.h"

/* This example demonstrates how to create file server
 * using esp_http_server. This file has only startup code.
 * Look in file_server.c for the implementation.
 */

static const char *TAG = "main";

void setup_wifi(void)
{
    char ssid[WIFI_SSID_MAX_LEN];
    char passwd[WIFI_PASSWD_MAX_LEN];
    
    wifi_mode_t mode = query_wifi_mode();
    switch (mode) {
    case WIFI_MODE_STA:
    case WIFI_MODE_AP:
        break;
    default:
        ESP_LOGE(TAG, "WiFi mode is configured properly");
        break;
    }

    if (query_wifi_ssid(ssid, WIFI_SSID_MAX_LEN) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WiFi SSID from config");
        return;
    }

    if (query_wifi_passwd(passwd, WIFI_PASSWD_MAX_LEN) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WiFi password from config");
        return;
    }

    esp_err_t ret = wifi_init(mode, ssid, passwd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi, mode=%d, ssid=%s", mode, ssid);
        return;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting example");
    ESP_ERROR_CHECK(nvs_flash_init());

    // esp_netif_init and esp_event_loop_create_default are called by wifi_init
    setup_wifi();
    
    /* Initialize file storage */
    const char* base_path = "/data";
    ESP_ERROR_CHECK(example_mount_storage(base_path));

    /* Start the file server */
    ESP_ERROR_CHECK(example_start_file_server(base_path));
    ESP_LOGI(TAG, "File server started");
}
