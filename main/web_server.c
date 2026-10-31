#include "web_server.h"
#include "letter_config.h"
#include "letter_display.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "web_server";

// embedded through EMBED_TXTFILES
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static cJSON *parse_json_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= (int)buf_size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty or oversized body");
        return NULL;
    }

    int received = httpd_req_recv(req, buf, total_len);
    if (received <= 0) {
        httpd_resp_send_500(req);
        return NULL;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
    }
    return root;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    size_t len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start, len);
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    letter_config_t cfgs[LETTER_CONFIG_COUNT];
    letter_config_get_all(cfgs);

    uint32_t random_duration_ms = 0, random_interval_ms = 0;
    bool random_enabled = false;
    uint16_t random_transition_ms = 0;
    letter_config_get_random(&random_duration_ms, &random_interval_ms, &random_enabled,
                              &random_transition_ms);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "delay_ms", letter_config_get_delay_ms());
    cJSON_AddNumberToObject(root, "gap_ms", letter_config_get_gap_ms());
    cJSON_AddNumberToObject(root, "random_duration_ms", random_duration_ms);
    cJSON_AddNumberToObject(root, "random_interval_ms", random_interval_ms);
    cJSON_AddBoolToObject(root, "random_enabled", random_enabled);
    cJSON_AddNumberToObject(root, "random_transition_ms", random_transition_ms);

    cJSON *letters = cJSON_CreateArray();
    for (int i = 0; i < LETTER_CONFIG_COUNT; i++) {
        cJSON *obj = cJSON_CreateObject();
        char letter_str[2] = { (char)('A' + i), '\0' };
        cJSON_AddStringToObject(obj, "letter", letter_str);
        cJSON_AddNumberToObject(obj, "start", cfgs[i].start_led);
        cJSON_AddNumberToObject(obj, "end", cfgs[i].end_led);
        cJSON_AddNumberToObject(obj, "r", cfgs[i].color.r);
        cJSON_AddNumberToObject(obj, "g", cfgs[i].color.g);
        cJSON_AddNumberToObject(obj, "b", cfgs[i].color.b);
        cJSON_AddItemToArray(letters, obj);
    }
    cJSON_AddItemToObject(root, "letters", letters);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return err;
}

static esp_err_t config_letter_post_handler(httpd_req_t *req)
{
    char buf[160];
    cJSON *root = parse_json_body(req, buf, sizeof(buf));
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *letter_item = cJSON_GetObjectItem(root, "letter");
    cJSON *start_item = cJSON_GetObjectItem(root, "start");
    cJSON *end_item = cJSON_GetObjectItem(root, "end");
    cJSON *r_item = cJSON_GetObjectItem(root, "r");
    cJSON *g_item = cJSON_GetObjectItem(root, "g");
    cJSON *b_item = cJSON_GetObjectItem(root, "b");

    bool valid = cJSON_IsString(letter_item) && strlen(letter_item->valuestring) == 1 &&
                 cJSON_IsNumber(start_item) && cJSON_IsNumber(end_item) &&
                 cJSON_IsNumber(r_item) && cJSON_IsNumber(g_item) && cJSON_IsNumber(b_item);

    if (!valid) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid fields");
        return ESP_FAIL;
    }

    letter_config_t cfg = {
        .start_led = (uint16_t)start_item->valueint,
        .end_led = (uint16_t)end_item->valueint,
        .color = {
            .r = (uint8_t)r_item->valueint,
            .g = (uint8_t)g_item->valueint,
            .b = (uint8_t)b_item->valueint,
        },
    };
    char letter = letter_item->valuestring[0];
    cJSON_Delete(root);

    esp_err_t err = letter_config_set(letter, cfg);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid LED range");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Letter %c config -> LEDs %u-%u RGB(%u,%u,%u)",
             letter, cfg.start_led, cfg.end_led, cfg.color.r, cfg.color.g, cfg.color.b);
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t config_timing_post_handler(httpd_req_t *req)
{
    char buf[64];
    cJSON *root = parse_json_body(req, buf, sizeof(buf));
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *delay_item = cJSON_GetObjectItem(root, "delay_ms");
    cJSON *gap_item = cJSON_GetObjectItem(root, "gap_ms");
    if (!cJSON_IsNumber(delay_item) || !cJSON_IsNumber(gap_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing delay_ms/gap_ms");
        return ESP_FAIL;
    }

    uint16_t delay_ms = (uint16_t)delay_item->valueint;
    uint16_t gap_ms = (uint16_t)gap_item->valueint;
    cJSON_Delete(root);

    if (letter_config_set_timing(delay_ms, gap_ms) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t config_random_post_handler(httpd_req_t *req)
{
    char buf[128];
    cJSON *root = parse_json_body(req, buf, sizeof(buf));
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *duration_item = cJSON_GetObjectItem(root, "duration_ms");
    cJSON *interval_item = cJSON_GetObjectItem(root, "interval_ms");
    cJSON *enabled_item = cJSON_GetObjectItem(root, "enabled");
    cJSON *transition_item = cJSON_GetObjectItem(root, "transition_ms");

    bool valid = cJSON_IsNumber(duration_item) && cJSON_IsNumber(interval_item) &&
                 cJSON_IsBool(enabled_item) && cJSON_IsNumber(transition_item);

    if (!valid) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid fields");
        return ESP_FAIL;
    }

    uint32_t duration_ms = (uint32_t)duration_item->valuedouble;
    uint32_t interval_ms = (uint32_t)interval_item->valuedouble;
    bool enabled = cJSON_IsTrue(enabled_item);
    uint16_t transition_ms = (uint16_t)transition_item->valueint;
    cJSON_Delete(root);

    if (letter_config_set_random(duration_ms, interval_ms, enabled, transition_ms) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Random config -> duration:%ums interval:%ums enabled:%d transition:%ums",
             duration_ms, interval_ms, enabled, transition_ms);
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t control_letter_post_handler(httpd_req_t *req)
{
    char buf[64];
    cJSON *root = parse_json_body(req, buf, sizeof(buf));
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *letter_item = cJSON_GetObjectItem(root, "letter");
    if (!cJSON_IsString(letter_item) || strlen(letter_item->valuestring) != 1) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing 'letter'");
        return ESP_FAIL;
    }

    char letter = letter_item->valuestring[0];
    cJSON_Delete(root);

    letter_display_show_single(letter);
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t control_text_post_handler(httpd_req_t *req)
{
    char buf[LETTER_DISPLAY_MAX_TEXT + 32];
    cJSON *root = parse_json_body(req, buf, sizeof(buf));
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *text_item = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(text_item) || strlen(text_item->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing 'text'");
        return ESP_FAIL;
    }

    esp_err_t err = letter_display_play_text(text_item->valuestring);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid text");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t control_stop_post_handler(httpd_req_t *req)
{
    letter_display_stop();
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t control_all_post_handler(httpd_req_t *req)
{
    char buf[32];
    cJSON *root = parse_json_body(req, buf, sizeof(buf));
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *on_item = cJSON_GetObjectItem(root, "on");
    if (!cJSON_IsBool(on_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing 'on'");
        return ESP_FAIL;
    }

    bool on = cJSON_IsTrue(on_item);
    cJSON_Delete(root);

    letter_display_set_all(on);
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t control_knight_rider_post_handler(httpd_req_t *req)
{
    char buf[32];
    cJSON *root = parse_json_body(req, buf, sizeof(buf));
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *on_item = cJSON_GetObjectItem(root, "on");
    if (!cJSON_IsBool(on_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing 'on'");
        return ESP_FAIL;
    }

    bool on = cJSON_IsTrue(on_item);
    cJSON_Delete(root);

    if (on) {
        letter_display_knight_rider_start();
    } else {
        letter_display_stop();
    }
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t control_status_get_handler(httpd_req_t *req)
{
    char text[LETTER_DISPLAY_MAX_TEXT];
    size_t idx = 0;
    bool running = false;
    letter_display_get_progress(text, sizeof(text), &idx, &running);

    const char *mode = "idle";
    switch (letter_display_get_state()) {
        case LETTER_DISPLAY_PLAYING:      mode = "text"; break;
        case LETTER_DISPLAY_RANDOM:       mode = "random"; break;
        case LETTER_DISPLAY_TRANSITION:   mode = "transition"; break;
        case LETTER_DISPLAY_ALL_ON:       mode = "all_on"; break;
        case LETTER_DISPLAY_KNIGHT_RIDER: mode = "knight_rider"; break;
        default:                          mode = "idle"; break;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "running", running);
    cJSON_AddStringToObject(root, "mode", mode);
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddNumberToObject(root, "index", (double)idx);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return err;
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t routes[] = {
        { .uri = "/",                    .method = HTTP_GET,  .handler = root_get_handler },
        { .uri = "/api/config",          .method = HTTP_GET,  .handler = config_get_handler },
        { .uri = "/api/config/letter",   .method = HTTP_POST, .handler = config_letter_post_handler },
        { .uri = "/api/config/timing",   .method = HTTP_POST, .handler = config_timing_post_handler },
        { .uri = "/api/config/random",   .method = HTTP_POST, .handler = config_random_post_handler },
        { .uri = "/api/control/letter",  .method = HTTP_POST, .handler = control_letter_post_handler },
        { .uri = "/api/control/text",    .method = HTTP_POST, .handler = control_text_post_handler },
        { .uri = "/api/control/stop",    .method = HTTP_POST, .handler = control_stop_post_handler },
        { .uri = "/api/control/all",         .method = HTTP_POST, .handler = control_all_post_handler },
        { .uri = "/api/control/knight_rider", .method = HTTP_POST, .handler = control_knight_rider_post_handler },
        { .uri = "/api/control/status",  .method = HTTP_GET,  .handler = control_status_get_handler },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP server listening on port 80");
    return ESP_OK;
}
