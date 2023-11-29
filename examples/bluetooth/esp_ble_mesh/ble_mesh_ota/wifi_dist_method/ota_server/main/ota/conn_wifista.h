/* connection.h - Wifi Connection utility */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PSWD_MAX_LEN 64

/* The event group allows multiple bits for each event, the relevant events are:
 * - getting connected to the AP with an IP
 * - failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#if CONFIG_EXAMPLE_WIFI_SCAN_METHOD_FAST
#define EXAMPLE_WIFI_SCAN_METHOD WIFI_FAST_SCAN
#elif CONFIG_EXAMPLE_WIFI_SCAN_METHOD_ALL_CHANNEL
#define EXAMPLE_WIFI_SCAN_METHOD WIFI_ALL_CHANNEL_SCAN
#endif

#if CONFIG_EXAMPLE_WIFI_CONNECT_AP_BY_SIGNAL
#define EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#elif CONFIG_EXAMPLE_WIFI_CONNECT_AP_BY_SECURITY
#define EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SECURITY
#endif

#if CONFIG_EXAMPLE_WIFI_AUTH_OPEN
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_EXAMPLE_WIFI_AUTH_WEP
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA2_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA_WPA2_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA2_ENTERPRISE
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_ENTERPRISE
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA3_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA2_WPA3_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WAPI_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif


#define WIFI_SCAN_METHOD EXAMPLE_WIFI_SCAN_METHOD
#define WIFI_SORT_METHOD EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD
#define WIFI_THRESHOLD_RSSI CONFIG_EXAMPLE_WIFI_SCAN_RSSI_THRESHOLD
#define WIFI_THRESHOLD_AUTHMODE EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD
#define WIFI_RETRY_LIMIT CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY

/**
 * @brief   Set the WiFi SSID
 * 
 * @param[in]   ssid    WiFi SSID
 * @param[in]   len     Length of the SSID
 * 
 * @return ESP_OK on success; any other value indicates an error
*/
esp_err_t set_wifi_ssid(uint8_t *ssid, uint16_t len);

/**
 * @brief   Set the WiFi password
 * 
 * @param[in]   password    WiFi password
 * @param[in]   len         Length of the password
 * 
 * @return ESP_OK on success; any other value indicates an error
*/
esp_err_t set_wifi_password(uint8_t *password, uint16_t len);

/**
 * @brief   Connect to the WiFi AP
 * @note    The WiFi SSID and password must be set before calling this function
 * 
 * @return ESP_OK on success; any other value indicates an error
*/
esp_err_t simple_connect(void);