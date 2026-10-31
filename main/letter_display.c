#include "letter_display.h"
#include "letter_config.h"
#include "led_control.h"

#include <string.h>
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "letter_display";

#define KNIGHT_RIDER_STEP_MS 60
#define KNIGHT_RIDER_TAIL    3

typedef enum {
    PHASE_ON,
    PHASE_OFF,
} playing_phase_t;

static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_timer;
static letter_display_state_t s_state = LETTER_DISPLAY_IDLE;

static char s_text[LETTER_DISPLAY_MAX_TEXT];
static size_t s_len;
static size_t s_idx;
static playing_phase_t s_phase;

static uint32_t s_random_ticks_left;

static int s_kr_pos;
static int s_kr_dir = 1;
static int s_kr_last_index;

// All helpers below expect s_lock to be held by the caller

static void apply_letter(char c)
{
    led_control_clear_all();

    if (c == ' ') {
        return;
    }

    letter_config_t cfg;
    if (letter_config_get(c, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "No config for '%c'", c);
        return;
    }

    for (uint32_t i = cfg.start_led; i <= cfg.end_led; i++) {
        led_control_set_pixel((uint16_t)i, cfg.color);
    }
    led_control_refresh();
}

static void apply_random_letter(void)
{
    apply_letter((char)('A' + (esp_random() % 26)));
}

// Highest end_led across all letters, so the scanner stays inside the used area
static int knight_rider_last_index(void)
{
    letter_config_t cfgs[LETTER_CONFIG_COUNT];
    letter_config_get_all(cfgs);

    int max_end = 0;
    for (int i = 0; i < LETTER_CONFIG_COUNT; i++) {
        if ((int)cfgs[i].end_led > max_end) {
            max_end = cfgs[i].end_led;
        }
    }
    return max_end;
}

static bool color_for_led(const letter_config_t cfgs[LETTER_CONFIG_COUNT], int idx, led_color_t *out)
{
    for (int i = 0; i < LETTER_CONFIG_COUNT; i++) {
        if (idx >= (int)cfgs[i].start_led && idx <= (int)cfgs[i].end_led) {
            *out = cfgs[i].color;
            return true;
        }
    }
    return false;
}

static void apply_knight_rider_frame(void)
{
    led_control_clear_all();

    letter_config_t cfgs[LETTER_CONFIG_COUNT];
    letter_config_get_all(cfgs);

    for (int t = 0; t < KNIGHT_RIDER_TAIL; t++) {
        int idx = s_kr_pos - (s_kr_dir * t);
        if (idx < 0 || idx > s_kr_last_index) {
            continue;
        }

        led_color_t base;
        if (!color_for_led(cfgs, idx, &base)) {
            continue;
        }

        uint8_t fade = (uint8_t)(255 / (t + 1));
        led_color_t c = {
            .r = (uint8_t)((base.r * fade) / 255),
            .g = (uint8_t)((base.g * fade) / 255),
            .b = (uint8_t)((base.b * fade) / 255),
        };
        led_control_set_pixel((uint16_t)idx, c);
    }
    led_control_refresh();
}

static void start_playing_message(void)
{
    ESP_LOGI(TAG, "Playing \"%s\"", s_text);
    s_state = LETTER_DISPLAY_PLAYING;
    s_idx = 0;
    s_phase = PHASE_ON;
    apply_letter(s_text[0]);
    esp_timer_start_once(s_timer, (uint64_t)letter_config_get_delay_ms() * 1000);
}

static void start_transition(void)
{
    uint16_t transition_ms = 0;
    letter_config_get_random(NULL, NULL, NULL, &transition_ms);

    if (transition_ms == 0) {
        start_playing_message();
        return;
    }

    s_state = LETTER_DISPLAY_TRANSITION;
    led_control_clear_all();
    esp_timer_start_once(s_timer, (uint64_t)transition_ms * 1000);
}

static void start_random_sequence(void)
{
    uint32_t duration_ms = 0, interval_ms = 0;
    letter_config_get_random(&duration_ms, &interval_ms, NULL, NULL);

    if (interval_ms == 0) {
        interval_ms = 200;
    }

    s_state = LETTER_DISPLAY_RANDOM;
    s_random_ticks_left = duration_ms / interval_ms;
    if (s_random_ticks_left == 0) {
        s_random_ticks_left = 1;
    }

    apply_random_letter();
    esp_timer_start_once(s_timer, (uint64_t)interval_ms * 1000);
}

static void timer_cb(void *arg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_state == LETTER_DISPLAY_RANDOM) {
        if (s_random_ticks_left <= 1) {
            start_transition();
        } else {
            s_random_ticks_left--;
            apply_random_letter();

            uint32_t interval_ms = 200;
            letter_config_get_random(NULL, &interval_ms, NULL, NULL);
            if (interval_ms == 0) {
                interval_ms = 200;
            }
            esp_timer_start_once(s_timer, (uint64_t)interval_ms * 1000);
        }
    } else if (s_state == LETTER_DISPLAY_TRANSITION) {
        start_playing_message();
    } else if (s_state == LETTER_DISPLAY_PLAYING) {
        if (s_phase == PHASE_ON) {
            led_control_clear_all();
            s_phase = PHASE_OFF;
            esp_timer_start_once(s_timer, (uint64_t)letter_config_get_gap_ms() * 1000);
        } else {
            s_idx++;
            if (s_idx >= s_len) {
                s_state = LETTER_DISPLAY_IDLE;
                led_control_clear_all();
            } else {
                apply_letter(s_text[s_idx]);
                s_phase = PHASE_ON;
                esp_timer_start_once(s_timer, (uint64_t)letter_config_get_delay_ms() * 1000);
            }
        }
    } else if (s_state == LETTER_DISPLAY_KNIGHT_RIDER) {
        s_kr_pos += s_kr_dir;
        if (s_kr_pos >= s_kr_last_index) {
            s_kr_pos = s_kr_last_index;
            s_kr_dir = -1;
        } else if (s_kr_pos <= 0) {
            s_kr_pos = 0;
            s_kr_dir = 1;
        }
        apply_knight_rider_frame();
        esp_timer_start_once(s_timer, (uint64_t)KNIGHT_RIDER_STEP_MS * 1000);
    }

    xSemaphoreGive(s_lock);
}

esp_err_t letter_display_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = &timer_cb,
        .name = "letter_seq",
    };
    return esp_timer_create(&timer_args, &s_timer);
}

esp_err_t letter_display_show_single(char letter)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_timer_stop(s_timer);
    s_state = LETTER_DISPLAY_IDLE;
    apply_letter(letter);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t letter_display_stop(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_timer_stop(s_timer);
    s_state = LETTER_DISPLAY_IDLE;
    led_control_clear_all();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t letter_display_play_text(const char *text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = strnlen(text, LETTER_DISPLAY_MAX_TEXT - 1);
    if (len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_timer_stop(s_timer);

    memcpy(s_text, text, len);
    s_text[len] = '\0';
    s_len = len;
    s_idx = 0;

    bool random_enabled = false;
    letter_config_get_random(NULL, NULL, &random_enabled, NULL);

    if (random_enabled) {
        start_random_sequence();
    } else {
        start_playing_message();
    }

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t letter_display_set_all(bool on)
{
    if (!on) {
        return letter_display_stop();
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_timer_stop(s_timer);
    s_state = LETTER_DISPLAY_ALL_ON;

    led_control_clear_all();

    letter_config_t cfgs[LETTER_CONFIG_COUNT];
    letter_config_get_all(cfgs);
    for (int i = 0; i < LETTER_CONFIG_COUNT; i++) {
        for (uint32_t j = cfgs[i].start_led; j <= cfgs[i].end_led; j++) {
            led_control_set_pixel((uint16_t)j, cfgs[i].color);
        }
    }
    led_control_refresh();

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t letter_display_knight_rider_start(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_timer_stop(s_timer);

    s_kr_last_index = knight_rider_last_index();
    s_state = LETTER_DISPLAY_KNIGHT_RIDER;
    s_kr_pos = 0;
    s_kr_dir = 1;
    apply_knight_rider_frame();
    esp_err_t err = esp_timer_start_once(s_timer, (uint64_t)KNIGHT_RIDER_STEP_MS * 1000);

    xSemaphoreGive(s_lock);
    return err;
}

letter_display_state_t letter_display_get_state(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    letter_display_state_t st = s_state;
    xSemaphoreGive(s_lock);
    return st;
}

void letter_display_get_progress(char *out_text, size_t out_text_size,
                                 size_t *out_index, bool *out_running)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (out_text != NULL && out_text_size > 0) {
        strncpy(out_text, s_text, out_text_size - 1);
        out_text[out_text_size - 1] = '\0';
    }
    if (out_index != NULL) {
        *out_index = s_idx;
    }
    if (out_running != NULL) {
        *out_running = (s_state != LETTER_DISPLAY_IDLE);
    }
    xSemaphoreGive(s_lock);
}
