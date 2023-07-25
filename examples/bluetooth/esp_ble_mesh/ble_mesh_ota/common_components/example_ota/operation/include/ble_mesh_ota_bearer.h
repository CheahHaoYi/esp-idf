// Copyright 2020-2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _BLE_MESH_OTA_BEARER_H_
#define _BLE_MESH_OTA_BEARER_H_

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "ble_mesh_ota_common.h"
#include "ble_mesh_ota_model_common.h"

#define BLE_MESH_OTA_WIFI_MAX_RETRY_CNT 0x0A // retry 10 times

#define BLE_MESH_OTA_WIFI_STA           0x00
#define BLE_MESH_OTA_WIFI_AP            0x01
#define BLE_MESH_OTA_WIFI_APSTA         0x02
#define BLE_MESH_OTA_WIFI_STA_FROM_URL  0x03 /* STA download firmware from server specifed url*/

#define BLE_MESH_OTA_IP_ADDR            "192.168.4.1"
#define BLE_MESH_OTA_TCP_PORT           8090

#define BLE_MESH_OTA_WIFI_SSID_LEN      4
#define BLE_MESH_OTA_WIFI_PWD_LEN       8

typedef struct {
    bool    ota_done;
    uint8_t wifi_role;
    uint8_t ssid[BLE_MESH_OTA_WIFI_SSID_LEN];
    uint8_t password[BLE_MESH_OTA_WIFI_PWD_LEN];
    uint8_t max_conn; /* Max number of stations allowed to connect in, default 4, max 4 (@ref esp_wifi_types.h) */
    char ota_url[OTA_URL_MAX_LENGTH];   /* optional */
    char url_ssid[OTA_SSID_MAX_LENGTH]; /* optional */
    char url_pass[OTA_PASS_MAX_LENGTH]; /* optional */
} ble_mesh_ota_wifi_init_t;

/**
 * @brief 
 * 
 * @param msg 
 * @param timeout 
 * @param to_front 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_ota_wifi_task_post(ble_mesh_ota_msg_t *msg, uint32_t timeout, bool to_front);

/**
 * @brief 
 * 
 * @param init 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_ota_bearer_init(ble_mesh_ota_wifi_init_t *init);

/**
 * @brief 
 * 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_ota_netif_init(void);

/**
 * @brief 
 * 
 * @param init 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_ota_wifi_init(ble_mesh_ota_wifi_init_t *init);

#endif /* _BLE_MESH_OTA_BEARER_H_ */
