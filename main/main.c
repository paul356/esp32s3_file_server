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

#include "config_db.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "file_serving_example_common.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "wifi_intf.h"

/* This example demonstrates how to create file server
 * using esp_http_server. This file has only startup code.
 * Look in file_server.c for the implementation.
 */

static const char *TAG = "main";

static void setup_wifi(void)
{
    char ssid[WIFI_SSID_MAX_LEN];
    char passwd[WIFI_PASSWD_MAX_LEN];

    wifi_mode_t mode = query_wifi_mode();
    switch (mode)
    {
    case WIFI_MODE_STA:
    case WIFI_MODE_AP:
        break;
    default:
        ESP_LOGE(TAG, "WiFi mode is configured properly");
        break;
    }

    if (query_wifi_ssid(ssid, WIFI_SSID_MAX_LEN) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read WiFi SSID from config");
        return;
    }

    if (query_wifi_passwd(passwd, WIFI_PASSWD_MAX_LEN) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read WiFi password from config");
        return;
    }

    esp_err_t ret = wifi_init(mode, ssid, passwd);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize WiFi, mode=%d, ssid=%s", mode, ssid);
        return;
    }
}

static esp_err_t reset_wifi(void)
{
    // assume wifi is already inited
    esp_err_t ret = save_wifi_mode(WIFI_MODE_AP);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save WiFi mode");
        return ret;
    }

    ret = save_wifi_ssid(CONFIG_EXAMPLE_DEFAULT_WIFI_AP_SSID);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save WiFi SSID");
        return ret;
    }

    ret = save_wifi_passwd(CONFIG_EXAMPLE_DEFAULT_WIFI_AP_PASSWD);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save WiFi password");
        return ret;
    }

    ret = wifi_update(WIFI_MODE_AP, CONFIG_EXAMPLE_DEFAULT_WIFI_AP_SSID, CONFIG_EXAMPLE_DEFAULT_WIFI_AP_PASSWD);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to update WiFi configuration");
        return ret;
    }

    return ESP_OK;
}

static void toggle_display_switch()
{
    bool display_on;
    esp_err_t ret = query_display_switch(&display_on);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read display switch config");
        return;
    }

    ret = save_display_switch(!display_on);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save display switch config");
        return;
    }
}

static QueueHandle_t gpio_intr_queue = NULL;

static void gpio_isr_handler(void *arg)
{
    gpio_num_t io_num = (gpio_num_t)arg;

    // ignore if queue is full
    (void)xQueueSendFromISR(gpio_intr_queue, &io_num, NULL);
}

static void gpio_intr_task(void *arg)
{
    gpio_num_t io_num;
    for (;;)
    {
        if (xQueueReceive(gpio_intr_queue, &io_num, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        switch (io_num)
        {
        case GPIO_NUM_18:
            ESP_LOGI(TAG, "GPIO 18 is triggered");
            toggle_display_switch();
            esp_restart();
            break;
        case GPIO_NUM_8:
            ESP_LOGI(TAG, "Will reset WiFi to AP mode");
            (void)reset_wifi();
            break;
        default:
            ESP_LOGE(TAG, "Unexpected GPIO %u is triggered", io_num);
            break;
        }
    }
}

static void setup_gpio_intr(void)
{
    gpio_intr_queue = xQueueCreate(5, sizeof(gpio_num_t));
    if (!gpio_intr_queue)
    {
        ESP_LOGE(TAG, "Failed to create queue for GPIO interrupt");
        return;
    }

    int ret = xTaskCreate(gpio_intr_task, "gpio_intr_task", 8192, NULL, 10, NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create task for GPIO interrupt");
        return;
    }

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1 << GPIO_NUM_18 | 1 << GPIO_NUM_8,
        .pull_down_en = 0,
        .pull_up_en = 1,
    };

    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_NUM_18, gpio_isr_handler, (void *)GPIO_NUM_18);
    gpio_isr_handler_add(GPIO_NUM_8, gpio_isr_handler, (void *)GPIO_NUM_8);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting example");
    ESP_ERROR_CHECK(nvs_flash_init());

    // esp_netif_init and esp_event_loop_create_default are called by wifi_init
    setup_wifi();

    setup_gpio_intr();

    // Initialize the display
    bool display_on;
    ESP_ERROR_CHECK(query_display_switch(&display_on));
    if (display_on)
    {
        ESP_ERROR_CHECK(display_init());
    }
    else
    {
        (void)turn_off_backlight();
    }

    /* Initialize file storage */
    const char *base_path = "/data";
    ESP_ERROR_CHECK(example_mount_storage(base_path));

    const char *web_path = "/spiffs";
    ESP_ERROR_CHECK(mount_web_storage(web_path));

    /* Start the file server */
    ESP_ERROR_CHECK(example_start_file_server(base_path, web_path));
    ESP_LOGI(TAG, "File server started");
}
