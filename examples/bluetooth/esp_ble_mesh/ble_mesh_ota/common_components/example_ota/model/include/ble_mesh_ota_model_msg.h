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

#ifndef _BLE_MESH_OTA_MODEL_MSG_H_
#define _BLE_MESH_OTA_MODEL_MSG_H_

#include <stdint.h>
#include "esp_ble_mesh_defs.h"
#include "ble_mesh_ota_model_common.h"

#define OTA_MSG_TTL     7 // BLE Mesh OTA mesaage TTL
#define OTA_MSG_CNT     7 // BLE Mesh OTA mesaage count
#define OTA_MSG_TIMEOUT 3000 // BLE Mesh OTA mesaage timeout

typedef struct {
    ble_mesh_bin_id_t     bin_id;
    ble_mesh_bin_ver_t    version;
    ble_mesh_bin_subver_t sub_version;
    uint8_t               flags;
    uint16_t              group_addr;
} ble_mesh_new_bin_versino_ntf_t;

typedef struct {
    ble_mesh_bin_id_t     bin_id;
    ble_mesh_bin_ver_t    version;
    ble_mesh_bin_subver_t sub_version;
} ble_mesh_need_ota_update_ntf_t;

typedef struct {
    ble_mesh_bin_id_t     bin_id;
    ble_mesh_bin_ver_t    version;
    ble_mesh_bin_subver_t sub_version;
} ble_mesh_ota_completed_ntf_t;

typedef struct {
    ble_mesh_ota_role_t role; /* Indicate the role of the device (phone or ESP32) which starts OTA procedure */
    uint16_t ssid;
    uint16_t password;
    char ota_url[OTA_URL_MAX_LENGTH];   /* optional */
    char url_ssid[OTA_SSID_MAX_LENGTH]; /* optional */
    char url_pass[OTA_PASS_MAX_LENGTH]; /* optional */
} ble_mesh_ota_update_start_t;

typedef struct {
    uint8_t status;
} ble_mesh_ota_update_status_t;

typedef struct {
    ble_mesh_bin_id_t     bin_id;
    ble_mesh_bin_ver_t    version;
    ble_mesh_bin_subver_t sub_version;
} ble_mesh_ota_current_version_status_t;

/**
 * @brief
 *
 * @param model
 * @param net_idx
 * @param app_idx
 * @param dst
 * @param ntf
 * @return esp_err_t
 */
esp_err_t ble_mesh_send_new_bin_versin_ntf(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_new_bin_versino_ntf_t *ntf);

/**
 * @brief
 *
 * @param model
 * @param app_idx
 * @param dst
 * @param ntf
 * @return esp_err_t
 */
esp_err_t ble_mesh_publish_need_ota_update_ntf(esp_ble_mesh_model_t *model, uint16_t app_idx,
        uint16_t dst, ble_mesh_need_ota_update_ntf_t *ntf);

/**
 * @brief
 *
 * @param model
 * @param net_idx
 * @param app_idx
 * @param dst
 * @param ntf
 * @return esp_err_t
 */
esp_err_t ble_mesh_send_current_version_status(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_ota_current_version_status_t *ocvs);

/**
 * @brief
 *
 * @param model
 * @param net_idx
 * @param app_idx
 * @param dst
 * @param ntf
 * @return esp_err_t
 */
esp_err_t ble_mesh_send_need_ota_update_ntf(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_need_ota_update_ntf_t *ntf);

/**
 * @brief
 *
 * @param model
 * @param net_idx
 * @param app_idx
 * @param dst
 * @param status
 * @return esp_err_t
 */
esp_err_t ble_mesh_send_ota_update_status(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_ota_update_status_t *status);

/**
 * @brief
 *
 * @param model
 * @param net_idx
 * @param app_idx
 * @param dst
 * @param start
 * @param contain_url
 * @return esp_err_t
 */
esp_err_t ble_mesh_send_ota_update_start(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_ota_update_start_t *start, bool contain_url);

/**
 * @brief
 *
 * @param model
 * @param app_idx
 * @param dst
 * @param ntf
 * @return esp_err_t
 */
esp_err_t ble_mesh_publish_ota_completed_ntf(esp_ble_mesh_model_t *model, uint16_t app_idx,
        uint16_t dst, ble_mesh_ota_completed_ntf_t *ntf);

#endif /* _BLE_MESH_OTA_MODEL_MSG_H_ */
