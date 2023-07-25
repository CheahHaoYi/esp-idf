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

#include <stdint.h>

#include "esp_log.h"
#include "esp_ble_mesh_defs.h"

#include "test.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_proxy_api.h"

#include "ble_mesh_example_nvs.h"
#include "ble_mesh_ota_model_common.h"
#include "ble_mesh_ota_model_msg.h"
#include "ble_mesh_ota_client_model.h"
#include "ble_mesh_ota_utility.h"

#define TAG "ota_cli"

#define OTA_CLIENT_WIFI_CHANGE_TIMEOUT_US       10000000 /* 10s */

static esp_timer_handle_t g_client_wifi_change_timer;

static void client_wifi_change_timeout_cb(void *arg)
{
    static bool wifi_change_flag = false;

    ESP_LOGD(TAG, "%s", __FUNCTION__);

    if (wifi_change_flag) {
        ESP_LOGW(TAG, "%s, OTA Client wifi already change", __FUNCTION__);
        return;
    }

    wifi_change_flag = true;

    /* In this case, we may need to temporarily disable the BLE scan functionality
     * to achieve the best Wi-Fi performance.
     */
    ESP_LOGD(TAG, "proxy gatt disable");
    esp_ble_mesh_proxy_gatt_disable();
    ESP_LOGD(TAG, "BLE Mesh scan stop, Start Wi-Fi");
    bt_mesh_test_stop_scanning();
    vTaskDelay(pdMS_TO_TICKS(100));
    ble_mesh_ota_start();
}

static ota_target_device_t *get_ready_ota_device(uint16_t addr)
{
    if (!ESP_BLE_MESH_ADDR_IS_UNICAST(addr)) {
        ESP_LOGE(TAG, "Not a unicast address 0x%04x", addr);
        return NULL;
    }

    for (uint8_t i = 0; i < BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM; i++) {
        if (ota_client_data.ready_ota_device[i].node_addr == addr) {
            return &ota_client_data.ready_ota_device[i];
        }
    }

    return NULL;
}

static esp_err_t store_ready_ota_device(uint16_t addr)
{
    if (!ESP_BLE_MESH_ADDR_IS_UNICAST(addr)) {
        ESP_LOGE(TAG, "Not a unicast address 0x%04x", addr);
        return ESP_FAIL;
    }

    for (uint8_t i = 0; i < BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM; i++) {
        if (ota_client_data.ready_ota_device[i].node_addr == addr) {
            ESP_LOGW(TAG, "Unicast address 0x%04x already exists", addr);
            return ESP_OK;
        }
    }

    for (uint8_t i = 0; i < BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM; i++) {
        if (ota_client_data.ready_ota_device[i].node_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
            ota_client_data.ready_ota_device[i].node_addr = addr;
            ota_client_data.ready_ota_device[i].ous_recv  = false;
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

void ble_mesh_ota_client_recv(esp_ble_mesh_model_t *model, esp_ble_mesh_msg_ctx_t *ctx,
                              uint32_t opcode, uint16_t length, const uint8_t *msg)
{
    esp_err_t err = ESP_OK;

    if (!model || !ctx || !msg) {
        ESP_LOGE(TAG, "%s, Invalid arguments", __FUNCTION__);
        return;
    }

    if (model->vnd.model_id != BLE_MESH_VND_MODEL_ID_OTA_CLIENT || model->vnd.company_id != CID_ESP) {
        ESP_LOGE(TAG, "Invalid model_id 0x%04x, company_id 0x%04x", model->vnd.model_id, model->vnd.company_id);
        return;
    }

    switch (opcode) {
    case BLE_MESH_VND_MODEL_OP_NEED_OTA_UPDATE_NOTIFY: {
        ESP_LOGI(TAG, "Recv Need OTA Update Notification (NOUN) from 0x%04x", ctx->addr);
        if (length != BLE_MESH_NEED_OTA_UPDATE_NOTIFY_LEN) {
            ESP_LOGE(TAG, "Invalid Need OTA Update Notification length %d", length);
            break;
        }

        ota_target_device_t *ready_ota_device = get_ready_ota_device(ctx->addr);
        if (!ready_ota_device) {
            if (ota_client_data.ready_ota_device_num >= BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM) {
                ESP_LOGW(TAG, "Ready ota devices reach maximum limit, ready_ota_device_num: %d", ota_client_data.ready_ota_device_num);
                break;
            }

            uint16_t bin_id      = msg[1] << 8 | msg[0];
            uint8_t  version     = msg[2];
            uint8_t  sub_version = msg[3];
            ESP_LOGD(TAG, "bin id: 0x%02x, version: %d.%d.%d", bin_id, version, (sub_version >> 4) & 0x0f, sub_version & 0x0f);
            ESP_LOGD(TAG, "ota client data: bin_id: 0x%02x, version: %d.%d.%d", ota_client_data.bin_id, ota_client_data.version, (ota_client_data.sub_version >> 4) & 0x0f, ota_client_data.sub_version & 0x0f);

            if ((bin_id != ota_client_data.bin_id) || ((version << 8 | sub_version) >= (ota_client_data.version << 8 | ota_client_data.sub_version))) {
                ESP_LOGW(TAG, "Not need update device, address: 0x%04x", ctx->addr);
                break;
            }
        }

        /* Send OTA Update Start */
        ble_mesh_ota_update_start_t ous = {
            .role     = BLE_MESH_OTA_ROLE_BOARD,
            .ssid     = ota_nvs_data.ssid,
            .password = ota_nvs_data.password,
        };
        ESP_LOGI(TAG, "Sending OTA Update Start (OUS) to 0x%04x", ctx->addr);
        ESP_LOGD(TAG, "role: 0x%04x, ssid: 0x%04x, password: 0x%04x", ous.role, ous.ssid, ous.password);
        err = ble_mesh_send_ota_update_start(model, ctx->net_idx, ctx->app_idx, ctx->addr, &ous, false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send OTA Update Start");
            break;
        }

        if (!ready_ota_device) {
            if (store_ready_ota_device(ctx->addr) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store ready ota device addr: 0x%04x", ctx->addr);
                break;
            }

            /* "++" here in order to prevent from updating more than "BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM" devices */
            ota_client_data.ready_ota_device_num++;
        }
        break;
    }
    case BLE_MESH_VND_MODEL_OP_OTA_UPDATE_STATUS: {
        ESP_LOGI(TAG, "Recv OTA Update Status (OUS) from 0x%04x", ctx->addr);
        if (length != BLE_MESH_OTA_UPDATE_STATUS_LEN) {
            ESP_LOGE(TAG, "Invalid OTA Update Status length: %d", length);
            return;
        }

        ota_target_device_t *ready_ota_device = get_ready_ota_device(ctx->addr);
        if (!ready_ota_device) {
            ESP_LOGE(TAG, "This ready ota device isn't exist");
            return;
        }

        if (ready_ota_device->ous_recv == true) {
            ESP_LOGE(TAG, "This ready ota device ota update start already received");
            return;
        }

        uint8_t device_status = msg[0];
        ESP_LOGI(TAG, "Recv OTA Update Status 0x%02x from 0x%04x", device_status, ctx->addr);

        switch (device_status) {
        case BLE_MESH_OTA_UPDATE_STATUS_SUCCEED: {
            ESP_LOGI(TAG, "BLE_MESH_OTA_UPDATE_STATUS_SUCCEED, %d", __LINE__);

            ready_ota_device->ous_recv = true; /* OTA Update status is received */

            ota_nvs_data.dev_flag |= BLE_MESH_OTA_GET_DEVICE;
            ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
            err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store ota nvs data, err: 0x%04x", err);
            }

            if (g_client_wifi_change_timer == NULL) {
                ESP_LOGI(TAG, "client wifi change timer start, %d", __LINE__);
                esp_timer_create_args_t timer_args = {
                    .callback = &client_wifi_change_timeout_cb,
                    .name     = "client_wifi_change",
                };

                ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_client_wifi_change_timer));
                esp_timer_start_once(g_client_wifi_change_timer, OTA_CLIENT_WIFI_CHANGE_TIMEOUT_US);
            } else {
                ESP_LOGI(TAG, "client wifi change timer restart, %d", __LINE__);
                esp_timer_stop(g_client_wifi_change_timer);
                esp_timer_start_once(g_client_wifi_change_timer, OTA_CLIENT_WIFI_CHANGE_TIMEOUT_US);
            }
        }
        break;
        case BLE_MESH_OTA_UPDATE_STATUS_INVALID_PARAM:
            ESP_LOGI(TAG, "BLE_MESH_OTA_UPDATE_STATUS_INVALID_PARAM");
            break;
        case BLE_MESH_OTA_UPDATE_STATUS_STORE_FAIL:
            ESP_LOGI(TAG, "BLE_MESH_OTA_UPDATE_STATUS_STORE_FAIL");
            break;
        default:
            break;
        }
        break;
    }
    default:
        ESP_LOGE(TAG, "Invalid ota client message, opcode: 0x%06x", opcode);
        break;
    }
}
