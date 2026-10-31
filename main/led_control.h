#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

// A0/D0 on the XIAO ESP32-S3
#define LED_STRIP_GPIO       GPIO_NUM_1

// Set this to the real number of LEDs on the strip
#define LED_STRIP_TOTAL_LEDS 100

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_color_t;

esp_err_t led_control_init(void);

// Writes to the buffer only, call led_control_refresh() to push it out
esp_err_t led_control_set_pixel(uint16_t index, led_color_t color);
esp_err_t led_control_refresh(void);
esp_err_t led_control_clear_all(void);

esp_err_t led_control_get_pixel(uint16_t index, led_color_t *color);
uint16_t led_control_get_count(void);
