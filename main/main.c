#include "esp_log.h"
#include "nvs_flash.h"

#include "led_control.h"
#include "letter_config.h"
#include "letter_display.h"
#include "wifi_ap.h"
#include "web_server.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(led_control_init());
    ESP_ERROR_CHECK(letter_config_init());
    ESP_ERROR_CHECK(letter_display_init());
    ESP_ERROR_CHECK(wifi_ap_init());
    ESP_ERROR_CHECK(web_server_start());

    ESP_LOGI(TAG, "Ready, open http://192.168.4.1/");
}
