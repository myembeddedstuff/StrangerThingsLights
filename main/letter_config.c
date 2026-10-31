#include "letter_config.h"

#include <string.h>
#include <ctype.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "letter_config";

#define NVS_NAMESPACE     "st_lights"
#define NVS_KEY_LETTERS   "letters_cfg"
#define NVS_KEY_DELAY     "delay_ms"
#define NVS_KEY_GAP       "gap_ms"
#define NVS_KEY_RAND_DUR  "rand_dur"
#define NVS_KEY_RAND_INT  "rand_int"
#define NVS_KEY_RAND_EN   "rand_en"
#define NVS_KEY_TRANS     "trans_ms"

// Each setting has its own key so adding one later doesn't wipe the others
static letter_config_t s_letters[LETTER_CONFIG_COUNT];
static uint16_t s_delay_ms;
static uint16_t s_gap_ms;
static uint32_t s_random_duration_ms;
static uint32_t s_random_interval_ms;
static bool s_random_enabled;
static uint16_t s_transition_ms;
static SemaphoreHandle_t s_lock;

static int letter_index(char letter)
{
    letter = (char)toupper((unsigned char)letter);
    if (letter < 'A' || letter > 'Z') {
        return -1;
    }
    return letter - 'A';
}

static void load_default_letters(void)
{
    for (int i = 0; i < LETTER_CONFIG_COUNT; i++) {
        s_letters[i].start_led = (uint16_t)i;
        s_letters[i].end_led = (uint16_t)i;
        s_letters[i].color = (led_color_t){ .r = 255, .g = 140, .b = 0 };
    }
}

static void load_defaults_all(void)
{
    load_default_letters();
    s_delay_ms = 300;
    s_gap_ms = 150;
    s_random_duration_ms = 5000;
    s_random_interval_ms = 200;
    s_random_enabled = false;
    s_transition_ms = 500;
}

static esp_err_t save_letters_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, NVS_KEY_LETTERS, s_letters, sizeof(s_letters));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t letter_config_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed (%s), using defaults", esp_err_to_name(err));
        load_defaults_all();
        return ESP_OK;
    }

    size_t len = sizeof(s_letters);
    err = nvs_get_blob(handle, NVS_KEY_LETTERS, s_letters, &len);
    if (err != ESP_OK || len != sizeof(s_letters)) {
        load_default_letters();
        nvs_set_blob(handle, NVS_KEY_LETTERS, s_letters, sizeof(s_letters));
    }

    if (nvs_get_u16(handle, NVS_KEY_DELAY, &s_delay_ms) != ESP_OK) {
        s_delay_ms = 300;
        nvs_set_u16(handle, NVS_KEY_DELAY, s_delay_ms);
    }

    if (nvs_get_u16(handle, NVS_KEY_GAP, &s_gap_ms) != ESP_OK) {
        s_gap_ms = 150;
        nvs_set_u16(handle, NVS_KEY_GAP, s_gap_ms);
    }

    if (nvs_get_u32(handle, NVS_KEY_RAND_DUR, &s_random_duration_ms) != ESP_OK) {
        s_random_duration_ms = 5000;
        nvs_set_u32(handle, NVS_KEY_RAND_DUR, s_random_duration_ms);
    }

    if (nvs_get_u32(handle, NVS_KEY_RAND_INT, &s_random_interval_ms) != ESP_OK) {
        s_random_interval_ms = 200;
        nvs_set_u32(handle, NVS_KEY_RAND_INT, s_random_interval_ms);
    }

    uint8_t enabled_u8 = 0;
    if (nvs_get_u8(handle, NVS_KEY_RAND_EN, &enabled_u8) != ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_RAND_EN, enabled_u8);
    }
    s_random_enabled = (enabled_u8 != 0);

    if (nvs_get_u16(handle, NVS_KEY_TRANS, &s_transition_ms) != ESP_OK) {
        s_transition_ms = 500;
        nvs_set_u16(handle, NVS_KEY_TRANS, s_transition_ms);
    }

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Config loaded (delay=%u gap=%u random=%d)",
             s_delay_ms, s_gap_ms, s_random_enabled);
    return ESP_OK;
}

esp_err_t letter_config_get(char letter, letter_config_t *out)
{
    int idx = letter_index(letter);
    if (idx < 0 || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_letters[idx];
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t letter_config_set(char letter, letter_config_t cfg)
{
    int idx = letter_index(letter);
    if (idx < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg.end_led < cfg.start_led || cfg.end_led >= led_control_get_count()) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_letters[idx] = cfg;
    esp_err_t err = save_letters_to_nvs();
    xSemaphoreGive(s_lock);
    return err;
}

void letter_config_get_all(letter_config_t out[LETTER_CONFIG_COUNT])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(out, s_letters, sizeof(s_letters));
    xSemaphoreGive(s_lock);
}

uint16_t letter_config_get_delay_ms(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t v = s_delay_ms;
    xSemaphoreGive(s_lock);
    return v;
}

uint16_t letter_config_get_gap_ms(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t v = s_gap_ms;
    xSemaphoreGive(s_lock);
    return v;
}

esp_err_t letter_config_set_timing(uint16_t delay_ms, uint16_t gap_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_delay_ms = delay_ms;
    s_gap_ms = gap_ms;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, NVS_KEY_DELAY, delay_ms);
        if (err == ESP_OK) {
            err = nvs_set_u16(handle, NVS_KEY_GAP, gap_ms);
        }
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    xSemaphoreGive(s_lock);
    return err;
}

void letter_config_get_random(uint32_t *duration_ms, uint32_t *interval_ms, bool *enabled,
                              uint16_t *transition_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (duration_ms != NULL) {
        *duration_ms = s_random_duration_ms;
    }
    if (interval_ms != NULL) {
        *interval_ms = s_random_interval_ms;
    }
    if (enabled != NULL) {
        *enabled = s_random_enabled;
    }
    if (transition_ms != NULL) {
        *transition_ms = s_transition_ms;
    }
    xSemaphoreGive(s_lock);
}

esp_err_t letter_config_set_random(uint32_t duration_ms, uint32_t interval_ms, bool enabled,
                                   uint16_t transition_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_random_duration_ms = duration_ms;
    s_random_interval_ms = interval_ms;
    s_random_enabled = enabled;
    s_transition_ms = transition_ms;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, NVS_KEY_RAND_DUR, duration_ms);
        if (err == ESP_OK) {
            err = nvs_set_u32(handle, NVS_KEY_RAND_INT, interval_ms);
        }
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, NVS_KEY_RAND_EN, enabled ? 1 : 0);
        }
        if (err == ESP_OK) {
            err = nvs_set_u16(handle, NVS_KEY_TRANS, transition_ms);
        }
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    xSemaphoreGive(s_lock);
    return err;
}
