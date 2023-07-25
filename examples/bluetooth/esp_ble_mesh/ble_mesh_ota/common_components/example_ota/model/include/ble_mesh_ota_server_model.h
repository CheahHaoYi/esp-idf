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

#ifndef _BLE_MESH_OTA_SERVER_MODEL_H_
#define _BLE_MESH_OTA_SERVER_MODEL_H_

#include <stdint.h>
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_proxy_api.h"

typedef struct {
    esp_ble_mesh_model_t *model;

    uint16_t bin_id;
    uint8_t  curr_version;
    uint8_t  curr_sub_version;

    uint16_t peer_addr; /* Unicast address of the peer device (Phone/Board) */
} ble_mesh_ota_server_data_t;

/**
 * @brief
 *
 * @param model
 * @param ctx
 * @param opcode
 * @param length
 * @param msg
 */
void ble_mesh_ota_server_recv(esp_ble_mesh_model_t *model, esp_ble_mesh_msg_ctx_t *ctx,
                              uint32_t opcode, uint16_t length, const uint8_t *msg);

/**
 * @brief
 *
 * @param model
 * @return esp_err_t
 */
esp_err_t ble_mesh_ota_server_init(esp_ble_mesh_model_t *model);

/**
 * @brief 
 * 
 */
void ble_mesh_ota_server_clean(void);

#endif /* _BLE_MESH_OTA_SERVER_MODEL_H_ */
