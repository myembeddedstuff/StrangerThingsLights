#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define LETTER_DISPLAY_MAX_TEXT 128

typedef enum {
    LETTER_DISPLAY_IDLE,
    LETTER_DISPLAY_RANDOM,
    LETTER_DISPLAY_TRANSITION,
    LETTER_DISPLAY_PLAYING,
    LETTER_DISPLAY_ALL_ON,
    LETTER_DISPLAY_KNIGHT_RIDER,
} letter_display_state_t;

esp_err_t letter_display_init(void);

// Any of the calls below cancels whatever was running before
esp_err_t letter_display_show_single(char letter);
esp_err_t letter_display_stop(void);
esp_err_t letter_display_play_text(const char *text);
esp_err_t letter_display_set_all(bool on);
esp_err_t letter_display_knight_rider_start(void);

letter_display_state_t letter_display_get_state(void);
void letter_display_get_progress(char *out_text, size_t out_text_size,
                                 size_t *out_index, bool *out_running);
