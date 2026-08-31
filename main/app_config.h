#pragma once

/* Board: YD-ESP32-23 / ESP32-S3 N16R8 */
#define APP_RGB_GPIO            48

/* INMP441: SCK=GPIO4, WS=GPIO5, SD=GPIO6, L/R=GND (left channel). */
#define APP_I2S_BCLK_GPIO       4
#define APP_I2S_WS_GPIO         5
#define APP_I2S_DATA_IN_GPIO    6

/* Replace these values before building. Never commit real credentials. */
#define APP_WIFI_SSID           "YOUR_WIFI_SSID"
#define APP_WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"

/* Fallback AP; use a unique password with at least eight characters. */
#define APP_SETUP_AP_SSID       "VoiceLights-Setup"
#define APP_SETUP_AP_PASSWORD   "ChangeMe123"

/* Dashboard address on the local network: http://voice-lights.local */
#define APP_MDNS_HOSTNAME       "voice-lights"
