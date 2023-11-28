/* main.c - Application main entry point */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_timer.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"

#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"
#include "board.h"

// #include "protocol_examples_common.h" // for stdin config
#include "conn_enow.h"


#define LOC_DESCR 0 // Location Descriptor
#define CID_ESP             0x02E5
#define PROV_OWN_ADDR       0x0001
#define PROV_START_ADDR     0x0005
#define DEVICE_UUID { 0x32, 0x10 }

#define MSG_SEND_TTL        3
#define MSG_SEND_REL        false
#define MSG_TIMEOUT         0
#define MSG_ROLE            ROLE_PROVISIONER

#define TRASMIT_COUNT 2
#define TRANSMIT_INTERVAL 20
#define DEFAULT_TTL 7

#define VND_MSG_MIN_LEN 2
#define COMP_DATA_PAGE_0    0x00

#define APP_KEY_IDX         0x0000
#define APP_KEY_OCTET       0x12

#define COMPOSITION_DATA_OFFSET 10

#define COMP_DATA_1_OCTET(msg, offset)      (msg[offset])
#define COMP_DATA_2_OCTET(msg, offset)      (msg[offset + 1] << 8 | msg[offset])

#define ESP_BLE_MESH_VND_MODEL_ID_CLIENT    0x0000
#define ESP_BLE_MESH_VND_MODEL_ID_SERVER    0x0001

#define ESP_BLE_MESH_VND_MODEL_OP_SEND      ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

#define ESP_BLE_MESH_VND_MODEL_OP_SSID_TRSF         ESP_BLE_MESH_MODEL_OP_3(0x02, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_PW_TRSF           ESP_BLE_MESH_MODEL_OP_3(0x03, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_OTA_URL_TRSF      ESP_BLE_MESH_MODEL_OP_3(0x04, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_OTA_SIZE_TRSF     ESP_BLE_MESH_MODEL_OP_3(0x05, CID_ESP)

#define ESP_BLE_MESH_VND_MODEL_OP_OTA_PROGRESS          ESP_BLE_MESH_MODEL_OP_3(0x06, CID_ESP)

#define ESP_BLE_MESH_VND_MODEL_OP_ESPNOW_UPDATE     ESP_BLE_MESH_MODEL_OP_3(0x07, CID_ESP)

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PSWD_MAX_LEN 64

#define OTA_URL_SIZE 256

// #define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS)
