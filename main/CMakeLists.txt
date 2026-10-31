idf_component_register(
    SRCS "main.c" "wifi_ap.c" "led_control.c" "letter_config.c" "letter_display.c" "web_server.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "index.html"
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server led_strip driver esp_driver_gpio esp_timer esp_hw_support
)
