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

#ifndef _BLE_MESH_OTA_MODEL_COMMON_H_
#define _BLE_MESH_OTA_MODEL_COMMON_H_

#include <stdint.h>
#include "nvs_flash.h"
#include "esp_ble_mesh_defs.h"

#define CID_ESP         0x02E5
#define OTA_NVS_KEY     "ota_data" // ota nvs data name

#define BLE_MESH_VND_MODEL_ID_OTA_SERVER                0x0002
#define BLE_MESH_VND_MODEL_ID_OTA_CLIENT                0x0003

#define BLE_MESH_VND_MODEL_OP_OTA_RESTART_NOTIFY        ESP_BLE_MESH_MODEL_OP_3(0x0A, CID_ESP)
#define BLE_MESH_VND_MODEL_OP_OTA_COMPLETED_NOTIFY      ESP_BLE_MESH_MODEL_OP_3(0x0B, CID_ESP)
#define BLE_MESH_VND_MODEL_OP_NEW_BIN_VERSION_NOTIFY    ESP_BLE_MESH_MODEL_OP_3(0x0C, CID_ESP)
#define BLE_MESH_VND_MODEL_OP_NEED_OTA_UPDATE_NOTIFY    ESP_BLE_MESH_MODEL_OP_3(0x0D, CID_ESP)
#define BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START          ESP_BLE_MESH_MODEL_OP_3(0x0E, CID_ESP)
#define BLE_MESH_VND_MODEL_OP_OTA_UPDATE_STATUS         ESP_BLE_MESH_MODEL_OP_3(0x0F, CID_ESP)
#define BLE_MESH_VND_MODEL_OP_GET_CURRENT_VERSION       ESP_BLE_MESH_MODEL_OP_3(0x10, CID_ESP)
#define BLE_MESH_VND_MODEL_OP_CURRENT_VERSION_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x11, CID_ESP)
#define BLE_MESH_VND_MODEL_OP_VERSION_ROLLBACK          ESP_BLE_MESH_MODEL_OP_3(0x12, CID_ESP)

/* Status code of OTA Update Status message */
#define BLE_MESH_OTA_UPDATE_STATUS_SUCCEED              0x00
#define BLE_MESH_OTA_UPDATE_STATUS_INVALID_PARAM        0x01
#define BLE_MESH_OTA_UPDATE_STATUS_STORE_FAIL           0x02

#define BLE_MESH_NEW_BIN_VERSION_NOTIFY_LEN             0x07
#define BLE_MESH_NEED_OTA_UPDATE_NOTIFY_LEN             0x04
#define BLE_MESH_OTA_COMPLETE_NOTIFY_LEN                0x04
#define BLE_MESH_OTA_UPDATE_START_LEN                   0x05
#define BLE_MESH_OTA_UPDATE_STATUS_LEN                  0x01

#define OTA_URL_MAX_LENGTH                              256
#define OTA_SSID_MAX_LENGTH                             32
#define OTA_PASS_MAX_LENGTH                             64

#define BLE_MESH_NODE_ADDR_STORED       BIT(0)
#define BLE_MESH_NODE_APPKEY_ADDED      BIT(1)
#define BLE_MESH_OTA_PARTION_ERASED     BIT(2)
#define BLE_MESH_OTA_NEED_UPDATE        BIT(3)
#define BLE_MESH_OTA_UPDATE_DONE        BIT(4)
#define BLE_MESH_OTA_GET_DEVICE         BIT(5)

#define BLE_MESH_BIN_ID_LIGHT_APP_IDX   0x0F00
#define BLE_MESH_BIN_ID_SENSOR_APP_IDX  0x0F01

extern nvs_handle NVS_HANDLE;

enum {
    BLE_MESH_OTA_ACT_NONE,
    BLE_MESH_OTA_ACT_NEED_OTA,
    BLE_MESH_OTA_ACT_OTA_DONE,
    BLE_MESH_OTA_ACT_INVALID,
};

typedef struct {
    uint8_t dev_flag; /* BIT0: 0 - own unicast address not stored, 1 - own unicast address stored */
                      /* BIT1: 0 - local appkey not added,         1 - local appkey added */
                      /* BIT2: 0 - ota partition not erased,       1 - ota partition erased */
                      /* BIT3: 0 - no ota update needed,           1 - need ota update */
                      /* BIT4: 0 - ota update not finished,        1 - ota update finished */
                      /* BIT5: 0 - no ota device to be updated,    1 - get an ota device to be updated */

    uint16_t own_addr;  /* Own unicast address */
    uint16_t ssid;      /* SSID of the AP to connect to. After OTA is done, ssid will be used by the device as its ssid */
    uint16_t password;  /* Password of AP to connect to. After OTA is done, password will be used by the device as its password */

    uint16_t bin_id;            /* Device bin id */
    uint8_t  curr_version;      /* Current bin version */
    uint8_t  curr_sub_version;  /* Current bin sub version */
    uint8_t  next_version;      /* Next bin version */
    uint8_t  next_sub_version;  /* Next bin sub version */

    uint8_t  flag;              /* BIT0: 0 - Not erase mesh stack data, 1 - Erase mesh stack data, 2 - OTA Data From URL Server */
    uint16_t group_addr;        /* Group address to publish Need OTA Update Notification */

    uint8_t  peer_role;         /* Role of the peer device (Phone/Board) which decides the role we should switch to (SOFTAP/STA) */
    uint16_t peer_addr;         /* Unicast Address of the peer device */
    char ota_url[OTA_URL_MAX_LENGTH];
    char url_ssid[OTA_SSID_MAX_LENGTH];
    char url_pass[OTA_PASS_MAX_LENGTH];
} __attribute__((packed)) ble_mesh_ota_nvs_data_t;

ble_mesh_ota_nvs_data_t ota_nvs_data;

typedef uint16_t ble_mesh_bin_id_t;
#define BLE_MESH_BIN_ID_LIGHT   0x0001
#define BLE_MESH_BIN_ID_SENSOR  0x0002

typedef uint8_t ble_mesh_ota_nbvn_flag_t;
#define BLE_MESH_STACK_DATA_NO_ERASE    0x00   /* Not erase mesh stack data stored in the flash */
#define BLE_MESH_STACK_DATA_ERASE       0x01   /* Erase mesh stack data stored in the flash */
#define BLE_MESH_STACK_DATA_FROM_URL    0x02   /* Download Firmware From URL */

typedef uint8_t ble_mesh_ota_role_t;
#define BLE_MESH_OTA_ROLE_PHONE         0x00
#define BLE_MESH_OTA_ROLE_BOARD         0x01

/* Version number */
typedef uint8_t ble_mesh_bin_ver_t;
enum {
    VERSION_0,
    VERSION_1,
    VERSION_2,
    VERSION_3,
    VERSION_4,
    VERSION_5,
    VERSION_MAX,
};

typedef uint8_t ble_mesh_bin_subver_t;
/* Most significant 4 bits of sub_version number */
enum {
    SUB_VERSION_MSB_0,
    SUB_VERSION_MSB_1,
    SUB_VERSION_MSB_2,
    SUB_VERSION_MSB_3,
    SUB_VERSION_MSB_4,
    SUB_VERSION_MSB_5,
    SUB_VERSION_MSB_6,
    SUB_VERSION_MSB_7,
    SUB_VERSION_MSB_8,
    SUB_VERSION_MSB_9,
    SUB_VERSION_MSB_10,
    SUB_VERSION_MSB_11,
    SUB_VERSION_MSB_12,
    SUB_VERSION_MSB_13,
    SUB_VERSION_MSB_14,
    SUB_VERSION_MSB_15,
    SUB_VERSION_MSB_MAX,
};
/* Least significant 4 bits of sub_version number */
enum {
    SUB_VERSION_LSB_0,
    SUB_VERSION_LSB_1,
    SUB_VERSION_LSB_2,
    SUB_VERSION_LSB_3,
    SUB_VERSION_LSB_4,
    SUB_VERSION_LSB_5,
    SUB_VERSION_LSB_6,
    SUB_VERSION_LSB_7,
    SUB_VERSION_LSB_8,
    SUB_VERSION_LSB_9,
    SUB_VERSION_LSB_10,
    SUB_VERSION_LSB_11,
    SUB_VERSION_LSB_12,
    SUB_VERSION_LSB_13,
    SUB_VERSION_LSB_14,
    SUB_VERSION_LSB_15,
    SUB_VERSION_LSB_MAX,
};

#endif /* _BLE_MESH_OTA_MODEL_COMMON_H_ */
