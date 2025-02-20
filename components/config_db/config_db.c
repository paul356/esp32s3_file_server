#include <string.h>
#include "nvs_util.h"
#include "sdkconfig.h"
#include "config_db.h"

#define CONFIG_NAMESPACE "CONFIG"

wifi_mode_t query_wifi_mode(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    size_t mode_len = sizeof(mode);
    esp_err_t ret = nvs_read_blob(CONFIG_NAMESPACE, "wifi_mode", &mode, &mode_len);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        return WIFI_MODE_NULL;
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        esp_err_t ret2 = save_wifi_mode(WIFI_MODE_AP);
        if (ret2 != ESP_OK) {
            return WIFI_MODE_NULL;
        } else {
            return WIFI_MODE_AP;
        }
    }

    return mode;
}

esp_err_t save_wifi_mode(wifi_mode_t mode)
{
    return nvs_write_blob(CONFIG_NAMESPACE, "wifi_mode", &mode, sizeof(mode));
}

esp_err_t query_wifi_ssid(char* ssid, size_t len)
{
    size_t ssid_len = len;
    esp_err_t ret = nvs_read_blob(CONFIG_NAMESPACE, "wifi_ssid", ssid, &ssid_len);

    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        return ret;
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        size_t req_len = strlcpy(ssid, CONFIG_EXAMPLE_DEFAULT_WIFI_AP_SSID, len);
        if (req_len >= len) {
            return ESP_ERR_NO_MEM;
        }

        esp_err_t ret2 = save_wifi_ssid(ssid);
        if (ret2 != ESP_OK) {
            return ret2;
        }
    }

    return ESP_OK;
}

esp_err_t save_wifi_ssid(const char* ssid)
{
    return nvs_write_blob(CONFIG_NAMESPACE, "wifi_ssid", ssid, strlen(ssid) + 1);
}

esp_err_t query_wifi_passwd(char* passwd, size_t len)
{
    size_t passwd_len = len;
    esp_err_t ret = nvs_read_blob(CONFIG_NAMESPACE, "wifi_passwd", passwd, &passwd_len);

    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        return ret;
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        size_t req_len = strlcpy(passwd, CONFIG_EXAMPLE_DEFAULT_WIFI_AP_PASSWD, len);
        if (req_len >= len) {
            return ESP_ERR_NO_MEM;
        }

        esp_err_t ret2 = save_wifi_passwd(passwd);
        if (ret2 != ESP_OK) {
            return ret2;
        }
    }

    return ESP_OK;
}

esp_err_t save_wifi_passwd(const char* passwd)
{
    return nvs_write_blob(CONFIG_NAMESPACE, "wifi_passwd", passwd, strlen(passwd) + 1);
}
