idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    REQUIRES espressif__esp-sr esp_wifi esp_event esp_netif esp_timer nvs_flash esp_http_server mdns
             esp_driver_i2s led_strip
)
