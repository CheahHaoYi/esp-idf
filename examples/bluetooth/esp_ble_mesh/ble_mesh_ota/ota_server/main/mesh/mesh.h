#pragma once

#include "esp_err.h"

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

#define LOC_DESCR 0 // Location Descriptor

#define CID_ESP 0x02E5 // 16 bit SIG Assigned Company identifier

#define DEFAULT_TTL 7

#define TRASMIT_COUNT 2
#define TRANSMIT_INTERVAL 20

#define VND_MSG_MIN_LEN 2
#define ONOFF_MSG_MIN_LEN (2 + 3) // 2 bytes for opcode, 3 bytes for payload

#define ESP_BLE_MESH_VND_MODEL_ID_CLIENT    0x0000
#define ESP_BLE_MESH_VND_MODEL_ID_SERVER    0x0001

// Define operations
#define ESP_BLE_MESH_VND_MODEL_OP_SEND      ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

#define ESP_BLE_MESH_VND_MODEL_OP_SSID_TRSF         ESP_BLE_MESH_MODEL_OP_3(0x02, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_PW_TRSF           ESP_BLE_MESH_MODEL_OP_3(0x03, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_OTA_URL_TRSF      ESP_BLE_MESH_MODEL_OP_3(0x04, CID_ESP)

esp_err_t ble_mesh_init(void);

