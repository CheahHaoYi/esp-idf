/* mesh.h - BLE Mesh Model Description */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#include "esp_bt.h"
#include "esp_bt_main.h"


#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_sensor_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

// From BLE Mesh Common Components
#include "ble_mesh_example_init.h"
#include "ota/ota.h"
#include "ota/conn_wifista.h"
#include "task/task.h"

#define LOC_DESCR 0 // Location Descriptor

#define CID_ESP 0x02E5 // 16 bit SIG Assigned Company identifier
#define DEVICE_UUID { 0x32, 0x10 }

#define DEFAULT_TTL 7

#define TRASMIT_COUNT 2
#define TRANSMIT_INTERVAL 20

#define VND_MSG_MIN_LEN 2
#define ONOFF_MSG_MIN_LEN (2 + 3) // 2 bytes for opcode, 3 bytes for payload

#define MSG_BUFFER_SIZE 256
#define MSG_TO_SEND_RELIABLY false

#define ESP_BLE_MESH_VND_MODEL_ID_CLIENT    0x0000
#define ESP_BLE_MESH_VND_MODEL_ID_SERVER    0x0001

// Define operations
#define ESP_BLE_MESH_VND_MODEL_OP_SEND      ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

#define ESP_BLE_MESH_VND_MODEL_OP_SSID_TRSF         ESP_BLE_MESH_MODEL_OP_3(0x02, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_PW_TRSF           ESP_BLE_MESH_MODEL_OP_3(0x03, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_OTA_URL_TRSF      ESP_BLE_MESH_MODEL_OP_3(0x04, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_OTA_SIZE_TRSF     ESP_BLE_MESH_MODEL_OP_3(0x05, CID_ESP)

#define ESP_BLE_MESH_VND_MODEL_OP_OTA_PROGRESS      ESP_BLE_MESH_MODEL_OP_3(0x06, CID_ESP)

#define ESP_BLE_MESH_VND_MODEL_OP_ESPNOW_UPDATE     ESP_BLE_MESH_MODEL_OP_3(0x07, CID_ESP)

#define RECEIVE_SSID_FLAG (1 << 0)
#define RECEIVE_PW_FLAG (1 << 1)
#define RECEIVE_OTA_URL_FLAG (1 << 2)
#define RECEIVE_OTA_SIZE_FLAG (1 << 3)
#define ALL_RECEIVED_FLAG (RECEIVE_SSID_FLAG | RECEIVE_PW_FLAG | RECEIVE_OTA_URL_FLAG | RECEIVE_OTA_SIZE_FLAG)

/**
 * @brief   Initialize BLE Mesh
 * 
 * @return  ESP_OK on success, any other value indicates error
*/
esp_err_t ble_mesh_init(void);

/**
 * @brief   Wrapper to send firmware download process to Mesh Client
 * 
 * @param[in]   ota_size   Size of the OTA image
*/
esp_err_t send_ota_size_update(uint64_t ota_size);

/**
 * @brief  Deinitialize BLE Mesh
 * 
 * @return  ESP_OK on success, any other value indicates error
*/
esp_err_t ble_mesh_deinit(void);
