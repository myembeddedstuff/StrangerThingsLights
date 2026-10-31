#include "led_control.h"

#include <string.h>
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "led_control";

static led_strip_handle_t s_strip;
static led_color_t s_colors[LED_STRIP_TOTAL_LEDS];
static SemaphoreHandle_t s_lock;

esp_err_t led_control_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_TOTAL_LEDS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Strip init failed: %s", esp_err_to_name(err));
        return err;
    }

    return led_strip_clear(s_strip);
}

esp_err_t led_control_set_pixel(uint16_t index, led_color_t color)
{
    if (index >= LED_STRIP_TOTAL_LEDS || s_strip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = led_strip_set_pixel(s_strip, index, color.r, color.g, color.b);
    if (err == ESP_OK) {
        s_colors[index] = color;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t led_control_refresh(void)
{
    if (s_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = led_strip_refresh(s_strip);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t led_control_clear_all(void)
{
    if (s_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = led_strip_clear(s_strip);
    if (err == ESP_OK) {
        memset(s_colors, 0, sizeof(s_colors));
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t led_control_get_pixel(uint16_t index, led_color_t *color)
{
    if (index >= LED_STRIP_TOTAL_LEDS || color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *color = s_colors[index];
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

uint16_t led_control_get_count(void)
{
    return LED_STRIP_TOTAL_LEDS;
}
