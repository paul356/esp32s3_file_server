/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * Copyright 2018 Gal Zaidenstein.
 * Copyright 2025 github.com/paul356
 */

#include "nvs.h"
#include "esp_log.h"

#define NVS_TAG "NVS Storage"

esp_err_t nvs_read_blob(const char* namespace, const char* key, void* buffer, size_t* buf_size)
{
	ESP_LOGI(NVS_TAG,"Opening NVS handle for %s", namespace);
    nvs_handle handle;
	esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		ESP_LOGE(NVS_TAG,"Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return err;
	}

    ESP_LOGI(NVS_TAG,"NVS Handle for %s opened successfully", namespace);
    
	//get blob array size
	err = nvs_get_blob(handle, key, buffer, buf_size);
	if (err != ESP_OK) {
		ESP_LOGE(NVS_TAG, "Error get key %s: %s", key, esp_err_to_name(err));
        nvs_close(handle);
        return err;
	}

	ESP_LOGI(NVS_TAG, "ns:%s key:%s copied to buffer", namespace, key);
	nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_write_blob(const char* namespace, const char* key, const void* buffer, size_t buf_size)
{
	ESP_LOGI(NVS_TAG,"Opening NVS handle");
    nvs_handle handle;
	esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		ESP_LOGE(NVS_TAG,"Error (%s) opening NVS handle for %s!\n", esp_err_to_name(err), namespace);
        return err;
    }

    ESP_LOGI(NVS_TAG,"NVS Handle opened successfully");

    err = nvs_set_blob(handle, key, buffer, buf_size);
	if (err != ESP_OK) {
		ESP_LOGE(NVS_TAG, "Error writing ns:%s key:%s layout: %s", namespace, key, esp_err_to_name(err));
        nvs_close(handle);
        return err;
	}

    ESP_LOGI(NVS_TAG, "Success writing layout");

    nvs_commit(handle);
	nvs_close(handle);
    return ESP_OK;
}
