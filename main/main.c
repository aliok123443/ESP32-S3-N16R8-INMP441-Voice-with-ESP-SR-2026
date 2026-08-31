#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

#define APP_SAMPLE_RATE_HZ       16000
#define APP_COMMAND_WINDOW_MS    7000

// TODO: move APP_RELAY_GPIO into app_config.h alongside the other GPIO
// definitions once you've picked a real pin. Left here as a fallback so the
// file still compiles standalone.
#ifndef APP_RELAY_GPIO
#define APP_RELAY_GPIO 21
#endif

// Most cheap relay modules (with an opto-isolator and a transistor driving
// the coil) are ACTIVE-LOW: the relay energizes when the GPIO is driven low.
// Set this to 0 if your module is active-high instead.
#ifndef APP_RELAY_ACTIVE_LOW
#define APP_RELAY_ACTIVE_LOW 0
#endif

enum voice_command_id {
    VOICE_COMMAND_LIGHTS = 1,
};

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static const char *TAG = "voice_lights";

static i2s_chan_handle_t s_i2s_rx;
static int last_command = -1;
static int64_t last_time = 0;

static srmodel_list_t *s_models;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t *s_afe_data;
static esp_mn_iface_t *s_multinet;
static model_iface_data_t *s_multinet_data;

static volatile bool s_wifi_connected;

static SemaphoreHandle_t s_relay_lock;
static volatile bool s_relay_on;

static void relay_apply_locked(bool on)
{
    s_relay_on = on;
#if APP_RELAY_ACTIVE_LOW
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(APP_RELAY_GPIO, on ? 0 : 1));
#else
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(APP_RELAY_GPIO, on ? 1 : 0));
#endif
    ESP_LOGI(TAG, "Relay/lights %s", on ? "ON" : "OFF");
}

static void relay_set(bool on)
{
    if (xSemaphoreTake(s_relay_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGW(TAG, "Relay is busy");
        return;
    }
    relay_apply_locked(on);
    xSemaphoreGive(s_relay_lock);
}

static void relay_toggle(void)
{
    if (xSemaphoreTake(s_relay_lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGW(TAG, "Relay is busy");
        return;
    }
    relay_apply_locked(!s_relay_on);
    xSemaphoreGive(s_relay_lock);
}

static bool relay_get(void)
{
    return s_relay_on;
}

static void relay_init(void)
{
    s_relay_lock = xSemaphoreCreateMutex();
    configASSERT(s_relay_lock != NULL);

    const gpio_config_t relay_io_conf = {
        .pin_bit_mask = 1ULL << APP_RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&relay_io_conf));
    relay_set(false); // start with the lights off
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected; the setup AP is still available");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        s_wifi_connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected. Dashboard: http://" IPSTR " or http://%s.local",
                 IP2STR(&event->ip_info.ip), APP_MDNS_HOSTNAME);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Dashboard client joined setup AP: " MACSTR, MAC2STR(event->mac));
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    wifi_config_t station_config = {
        .sta = {
            .ssid = APP_WIFI_SSID,
            .password = APP_WIFI_PASSWORD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = 8,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    wifi_config_t access_point_config = {
        .ap = {
            .ssid = APP_SETUP_AP_SSID,
            .ssid_len = sizeof(APP_SETUP_AP_SSID) - 1,
            .channel = 1,
            .password = APP_SETUP_AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.required = false},
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &access_point_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(APP_MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("Voice Lights"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));

    ESP_LOGI(TAG, "Setup AP: %s at http://192.168.4.1", APP_SETUP_AP_SSID);
}

static esp_err_t state_get_handler(httpd_req_t *request)
{
    char response[128];
    const int length = snprintf(response, sizeof(response),
                                "{\"wifi\":%s,\"listening\":true,\"relay\":%s}",
                                s_wifi_connected ? "true" : "false", relay_get() ? "true" : "false");
    if (length < 0 || length >= (int)sizeof(response)) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

static esp_err_t relay_post_handler(httpd_req_t *request)
{
    relay_toggle();
    return state_get_handler(request);
}

static esp_err_t root_get_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, index_html_start, index_html_end - index_html_start);
}

static esp_err_t redirect_404_handler(httpd_req_t *request, httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(request, "302 Temporary Redirect");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_sendstr(request, "Open the Voice Lights dashboard");
}

static void web_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_open_sockets = 5;

    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    const httpd_uri_t state = {
        .uri = "/api/state",
        .method = HTTP_GET,
        .handler = state_get_handler,
    };
    const httpd_uri_t relay = {
        .uri = "/api/relay",
        .method = HTTP_POST,
        .handler = relay_post_handler,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &state));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &relay));
    ESP_ERROR_CHECK(httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_404_handler));
}

static void voice_execute_command(int command_id)
{
    switch (command_id) {
    case VOICE_COMMAND_LIGHTS:
        ESP_LOGI(TAG, "Voice command: LIGHTS (toggle relay)");
        relay_toggle();
        break;
    default:
        ESP_LOGW(TAG, "Unknown voice command ID: %d", command_id);
        break;
    }
}

// No wake word: MultiNet listens on every AFE frame and is reset after
// each detection or internal timeout, so the device is always armed.
static void speech_fetch_task(void *arg)
{
    (void)arg;
    while (true) {
        // AFE hands back a processed audio frame; it does NOT run command
        // recognition itself. The frame still has to be fed to MultiNet.
        afe_fetch_result_t *fetch_result = s_afe->fetch(s_afe_data);
        if (fetch_result == NULL || fetch_result->data == NULL) {
            continue;
        }

        const esp_mn_state_t mn_state = s_multinet->detect(s_multinet_data, fetch_result->data);
        if (mn_state == ESP_MN_STATE_DETECTING) {
            continue;
        }

        if (mn_state == ESP_MN_STATE_TIMEOUT) {
            last_command = -1;
            last_time = 0;
            continue;
        }

        if (mn_state != ESP_MN_STATE_DETECTED) {
            continue;
        }

        esp_mn_results_t *mn_result = s_multinet->get_results(s_multinet_data);
        if (mn_result == NULL || mn_result->num <= 0) {
            continue;
        }

        const int cmd = mn_result->command_id[0];
        const int64_t now = esp_timer_get_time() / 1000; // milliseconds

        ESP_LOGI(TAG, "Recognized: %s (ID %d, %.2f)",
                 mn_result->string,
                 cmd,
                 (double)mn_result->prob[0]);

        if (cmd == last_command && (now - last_time) <= 2000) {
            ESP_LOGI(TAG, "Command confirmed");

            voice_execute_command(cmd);

            last_command = -1;
            last_time = 0;
        } else {
            last_command = cmd;
            last_time = now;
        }
    }
}

static void microphone_feed_task(void *arg)
{
    (void)arg;
    const int frame_samples = s_afe->get_feed_chunksize(s_afe_data);
    int32_t *raw_samples = calloc(frame_samples, sizeof(int32_t));
    int16_t *pcm_samples = calloc(frame_samples, sizeof(int16_t));
    configASSERT(raw_samples != NULL && pcm_samples != NULL);

    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_rx));
    ESP_LOGI(TAG, "INMP441 ready: 16 kHz I2S, GPIO BCLK=%d WS=%d DIN=%d", APP_I2S_BCLK_GPIO, APP_I2S_WS_GPIO,
             APP_I2S_DATA_IN_GPIO);

    while (true) {
        size_t bytes_read = 0;
        const esp_err_t read_result =
            i2s_channel_read(s_i2s_rx, raw_samples, frame_samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        if (read_result != ESP_OK || bytes_read != frame_samples * sizeof(int32_t)) {
            ESP_LOGW(TAG, "I2S frame incomplete: %s, %u bytes", esp_err_to_name(read_result), (unsigned)bytes_read);
            continue;
        }

        // INMP441 sends 24-bit samples left-aligned inside a 32-bit I2S slot.
        // The top 16 bits are exactly the signed PCM format expected by ESP-SR.
        for (int sample = 0; sample < frame_samples; ++sample) {
            pcm_samples[sample] = (int16_t)(raw_samples[sample] >> 16);
        }
        s_afe->feed(s_afe_data, pcm_samples);
    }
}

static void speech_init(void)
{
    s_models = esp_srmodel_init("model");
    configASSERT(s_models != NULL);

    char *multinet_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    configASSERT(multinet_name != NULL);
    ESP_LOGI(TAG, "Models: MultiNet=%s", multinet_name);

    s_multinet = esp_mn_handle_from_name(multinet_name);
    configASSERT(s_multinet != NULL);
    s_multinet_data = s_multinet->create(multinet_name, APP_COMMAND_WINDOW_MS);
    configASSERT(s_multinet_data != NULL);

    ESP_ERROR_CHECK(esp_mn_commands_alloc(s_multinet, s_multinet_data));
    ESP_ERROR_CHECK(esp_mn_commands_clear());
    // MultiNet5 accepts phonemes. Only the LIGHTS command remains now that
    // the RGB LED has been removed from the firmware.
    ESP_ERROR_CHECK(esp_mn_commands_phoneme_add(VOICE_COMMAND_LIGHTS, "LIGHTS", "LiTS"));
    configASSERT(esp_mn_commands_update() == NULL);
    s_multinet->print_active_speech_commands(s_multinet_data);

    afe_config_t *afe_config = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    configASSERT(afe_config != NULL);
    // Disable WakeNet: AFE will not gate on a wake word, and MultiNet runs
    // continuously against every incoming frame instead.
    afe_config->wakenet_init = false;
    s_afe = esp_afe_handle_from_config(afe_config);
    configASSERT(s_afe != NULL);
    s_afe_data = s_afe->create_from_config(afe_config);
    configASSERT(s_afe_data != NULL);
    afe_config_free(afe_config);
}

static void microphone_init(void)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 8;
    channel_config.dma_frame_num = 256;
    ESP_ERROR_CHECK(i2s_new_channel(&channel_config, NULL, &s_i2s_rx));

    i2s_std_config_t microphone_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(APP_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = APP_I2S_BCLK_GPIO,
            .ws = APP_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = APP_I2S_DATA_IN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    // INMP441 L/R is tied to GND, which selects the left I2S slot.
    microphone_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_rx, &microphone_config));
}

void app_main(void)
{
    relay_init();
    wifi_init();
    web_server_start();
    speech_init();
    microphone_init();

    xTaskCreatePinnedToCore(microphone_feed_task, "mic_feed", 6144, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(speech_fetch_task, "speech_fetch", 8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Ready. Always listening — say 'lights' to toggle the relay.");
}
