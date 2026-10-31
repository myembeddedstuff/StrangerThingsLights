#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "led_control.h"

#define LETTER_CONFIG_COUNT 26

typedef struct {
    uint16_t start_led;
    uint16_t end_led;   // inclusive
    led_color_t color;
} letter_config_t;

esp_err_t letter_config_init(void);

esp_err_t letter_config_get(char letter, letter_config_t *out);
esp_err_t letter_config_set(char letter, letter_config_t cfg);
void letter_config_get_all(letter_config_t out[LETTER_CONFIG_COUNT]);

// delay_ms: how long a letter stays on, gap_ms: blank time between letters
uint16_t letter_config_get_delay_ms(void);
uint16_t letter_config_get_gap_ms(void);
esp_err_t letter_config_set_timing(uint16_t delay_ms, uint16_t gap_ms);

// Random effect played before a message. transition_ms is the blank pause
// between the effect and the first letter.
void letter_config_get_random(uint32_t *duration_ms, uint32_t *interval_ms, bool *enabled,
                              uint16_t *transition_ms);
esp_err_t letter_config_set_random(uint32_t duration_ms, uint32_t interval_ms, bool enabled,
                                   uint16_t transition_ms);
