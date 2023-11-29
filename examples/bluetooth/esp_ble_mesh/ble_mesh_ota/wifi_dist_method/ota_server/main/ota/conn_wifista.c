/* connection.h - Wifi Connection utility */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "conn_wifista.h"

#define TAG "CONNECTION-WIFI-STA"

uint8_t client_received_ssid[WIFI_SSID_MAX_LEN];
uint8_t client_received_password[WIFI_PSWD_MAX_LEN];

static bool is_received_ssid = false;
static bool is_received_password = false;

static wifi_config_t wifi_config = {
    .sta = {
        .scan_method = WIFI_SCAN_METHOD,
        .sort_method = WIFI_SORT_METHOD,
        .threshold.rssi = WIFI_THRESHOLD_RSSI,
        .threshold.authmode = WIFI_THRESHOLD_AUTHMODE,
    },
};

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *sta_netif = NULL;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    static int retry_attempt_count = 0;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_STOP:
            ESP_LOGI(TAG, "WiFi STA stopped");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to the AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Connection to the AP fail");
            if (retry_attempt_count < WIFI_RETRY_LIMIT) {
                retry_attempt_count++;
                ESP_LOGI(TAG, "Reattempt connection to the AP");
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            break;
        default:
            ESP_LOGI(TAG, "Unhandled WIFI_EVENT: 0x%06" PRIx32, event_id);
            break;
        }
        return;
    } 
    
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP received:" IPSTR, IP2STR(&event->ip_info.ip));
        retry_attempt_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

esp_err_t set_wifi_ssid(uint8_t *ssid, uint16_t len)
{
    if (ssid == NULL) {
        ESP_LOGE(TAG, "Invalid SSID received");
        return ESP_ERR_INVALID_ARG;
    }

    if (len > WIFI_SSID_MAX_LEN) {
        ESP_LOGE(TAG, "SSID length exceeds maximum");
        return ESP_ERR_INVALID_ARG;
    }

    strncpy((char *)client_received_ssid, (char *)ssid, sizeof(client_received_ssid));
    is_received_ssid = true;

    return ESP_OK;
}

esp_err_t set_wifi_password(uint8_t *password, uint16_t len)
{
    if (password == NULL) {
        ESP_LOGE(TAG, "Invalid password received");
        return ESP_ERR_INVALID_ARG;
    }

    if (len > WIFI_PSWD_MAX_LEN) {
        ESP_LOGE(TAG, "Password length exceeds maximum");
        return ESP_ERR_INVALID_ARG;
    }

    strncpy((char *)client_received_password, (char *)password, sizeof(client_received_password));
    is_received_password = true;

    return ESP_OK;
}

/**
 * @brief   Update the WiFi configuration from buffer
*/
static esp_err_t update_wifi_config(void)
{
    memcpy(wifi_config.sta.ssid, client_received_ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, client_received_password, sizeof(wifi_config.sta.password));

    return ESP_OK;
}

/** 
 * @brief   Register event handler for WiFi events
*/
static esp_err_t register_handler(void)
{
    s_wifi_event_group = xEventGroupCreate();
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
                                                
    return ESP_OK;
}

esp_err_t simple_connect(void)
{
    if (!is_received_ssid || !is_received_password) {
        ESP_LOGI(TAG, "No credential received, abort WIFI connection");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to WiFi");
    if (sta_netif == NULL) {
        ESP_ERROR_CHECK(esp_netif_init());

        sta_netif = esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        register_handler();
    }
    
    update_wifi_config();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi Init done, waiting for connection.");

    /** Waiting until either 
     * the connection is established (WIFI_CONNECTED_BIT) 
     * or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). 
     * The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, 
        to test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to AP");
        return ESP_OK;
    } 
    
    ESP_LOGI(TAG, "Failed to connect to AP, try to receive credentials again");
    is_received_ssid = false;
    is_received_password = false;

    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Wifi connection failed");
        return ESP_ERR_WIFI_NOT_CONNECT;
    } 
        
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
    return ESP_FAIL;
}