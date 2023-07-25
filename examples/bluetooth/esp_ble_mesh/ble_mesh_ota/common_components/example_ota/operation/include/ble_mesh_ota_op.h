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

#ifndef _BLE_MESH_OTA_OP_H_
#define _BLE_MESH_OTA_OP_H_

#include <stdint.h>
#include "esp_err.h"
#include "esp_partition.h"

#include "ble_mesh_ota_common.h"

typedef struct {
    uint8_t ota_op;
    uint8_t max_dev;
    uint32_t total_bin_size;
    const esp_partition_t *ota_partition;
    esp_err_t (*ota_bearer_post_cb)(ble_mesh_ota_msg_t *msg, uint32_t timeout, bool to_front);
    esp_err_t (*ota_complete_cb)(uint8_t ota_op);
    esp_err_t (*ota_fail_cb)(uint8_t ota_op);
} ble_mesh_ota_op_init_t;

/**
 * @brief 
 * 
 * @return uint8_t 
 */
uint8_t ble_mesh_get_ota_dev_count(void);

/**
 * @brief 
 * 
 * @return uint16_t 
 */
uint16_t ble_mesh_get_ota_seg_count(void);

/**
 * @brief 
 * 
 * @param addr 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_store_send_ota_seg_device(const uint8_t addr[6]);

/**
 * @brief 
 * 
 * @param addr 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_store_recv_ota_seg_device(const uint8_t addr[6]);

/**
 * @brief 
 * 
 * @param addr 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_reset_ota_device(const uint8_t addr[6]);

/**
 * @brief 
 * 
 * @param msg 
 * @param timeout 
 * @param to_front 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_ota_op_task_post(ble_mesh_ota_msg_t *msg, uint32_t timeout, bool to_front);

/**
 * @brief 
 * 
 * @param init 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_ota_op_init(ble_mesh_ota_op_init_t *init);

/**
 * @brief 
 * 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_ota_task_init(void);

/**
 * @brief 
 * 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_ota_task_deinit(void);

/**
 * @brief 
 * 
 * @param init 
 * @return esp_err_t 
 */
esp_err_t ble_mesh_ota_op_init_post(ble_mesh_ota_op_init_t *init);

#endif /* _BLE_MESH_OTA_OP_H_ */
