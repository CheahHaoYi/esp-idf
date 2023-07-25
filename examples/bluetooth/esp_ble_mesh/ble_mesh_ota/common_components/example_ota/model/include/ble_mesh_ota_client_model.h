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

#ifndef _BLE_MESH_OTA_CLIENT_MODEL_H_
#define _BLE_MESH_OTA_CLIENT_MODEL_H_

#include <stdint.h>
#include <stdbool.h>

#include "esp_ble_mesh_defs.h"
#include "ble_mesh_ota_common.h"

typedef struct ota_target_device {
    uint16_t node_addr; /* Unicast address of the peer device */
    bool     ous_recv;  /* Indicate if OTA Update Status is received */
} ota_target_device_t;

typedef struct {
    uint16_t bin_id;
    uint8_t  version; // another ota partition firmware version
    uint8_t  sub_version; // another ota partition firmware version
    uint16_t group_addr;
    uint8_t  ready_ota_device_num;
    ota_target_device_t ready_ota_device[BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM];
} ble_mesh_ota_client_data_t;

ble_mesh_ota_client_data_t ota_client_data;

/**
 * @brief 
 * 
 * @param model 
 * @param ctx 
 * @param opcode 
 * @param length 
 * @param msg 
 */
void ble_mesh_ota_client_recv(esp_ble_mesh_model_t *model, esp_ble_mesh_msg_ctx_t *ctx,
                              uint32_t opcode, uint16_t length, const uint8_t *msg);

#endif /* _BLE_MESH_OTA_CLIENT_MODEL_H_ */
