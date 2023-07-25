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
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_proxy_api.h"

#include "test.h"
#include "ble_mesh_example_nvs.h"
#include "ble_mesh_ota_model_common.h"
#include "ble_mesh_ota_model_msg.h"
#include "ble_mesh_ota_server_model.h"
#include "ble_mesh_ota_utility.h"

#define TAG "ota_ser"

#define OTA_SERVER_WIFI_CHANGE_TIMEOUT_US 10000000 /* 10s */

static esp_timer_handle_t g_server_wifi_change_timer;
static ble_mesh_ota_server_data_t *server = NULL;

static void server_wifi_change_timeout_cb(void *arg)
{
    static bool wifi_change_flag = false;

    ESP_LOGD(TAG, "%s", __FUNCTION__);

    if (wifi_change_flag) {
        ESP_LOGW(TAG, "%s, OTA Server wifi already change", __FUNCTION__);
        return;
    }

    wifi_change_flag = true;

    /* In this case, we may need to temporarily disable the BLE scan functionality
     * to achieve the best Wi-Fi performance.
     */
    ESP_LOGD(TAG, "proxy gatt disable");
    esp_ble_mesh_proxy_gatt_disable();
    // ESP_LOGD(TAG, "BLE Mesh scan stop, Start Wi-Fi");
    // bt_mesh_test_stop_scanning();
    vTaskDelay(pdMS_TO_TICKS(100));
    ble_mesh_ota_start();
}

void ble_mesh_ota_server_clean(void)
{
    if (server) {
        server->peer_addr = ESP_BLE_MESH_ADDR_UNASSIGNED;
    }
}

void ble_mesh_ota_server_recv(esp_ble_mesh_model_t *model, esp_ble_mesh_msg_ctx_t *ctx,
                              uint32_t opcode, uint16_t length, const uint8_t *msg)
{
    esp_err_t err                 = ESP_OK;
    static    bool recv_nbvn_flag = false;

    if (ota_nvs_data.dev_flag & BLE_MESH_OTA_UPDATE_DONE) {
        /* If already upgraded, ota server shall ignore these messages. */
        ESP_LOGW(TAG, "This device already upgraded");
        return;
    }

    if (!model || !ctx || (!msg && opcode != BLE_MESH_VND_MODEL_OP_GET_CURRENT_VERSION)) {
        ESP_LOGE(TAG, "%s, Invalid arguments", __FUNCTION__);
        return;
    }

    if (model->vnd.model_id != BLE_MESH_VND_MODEL_ID_OTA_SERVER || model->vnd.company_id != CID_ESP) {
        ESP_LOGE(TAG, "Invalid model_id: 0x%04x, company_id: 0x%04x", model->vnd.model_id, model->vnd.company_id);
        return;
    }

    server = (ble_mesh_ota_server_data_t *)model->user_data;
    if (!server) {
        ESP_LOGE(TAG, "Invalid ota server user data");
        return;
    }

    switch (opcode) {
    case BLE_MESH_VND_MODEL_OP_GET_CURRENT_VERSION: {
        ble_mesh_ota_current_version_status_t current_ver = {0};
        current_ver.bin_id      = server->bin_id;
        current_ver.version     = server->curr_version;
        current_ver.sub_version = server->curr_sub_version;
        ESP_LOGI(TAG, "Sending OTA Current Version Status (OCVS) to 0x%04x, Bin ID 0x%04x, Version: 0x%04x, Sub Version: 0x%04x", ctx->addr, current_ver.bin_id, current_ver.version, current_ver.sub_version);
        err = ble_mesh_send_current_version_status(model, ctx->net_idx, ctx->app_idx, ctx->addr, &current_ver);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send OTA Current Version Status (OCVS)");
            return;
        }
        break;
    }
    case BLE_MESH_VND_MODEL_OP_VERSION_ROLLBACK: {
        ESP_LOGI(TAG, "BLE_MESH_VND_MODEL_OP_VERSION_ROLLBACK from 0x%04x", ctx->addr);
        const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);

        if (esp_ota_set_boot_partition(partition) == ESP_OK) {
            ESP_LOGI(TAG, "The next reboot will fall back to the previous version");
        } else {
            ESP_LOGE(TAG, "Rollback is not possible, do not have any suitable apps in slots");
        }
        break;
    }
    case BLE_MESH_VND_MODEL_OP_NEW_BIN_VERSION_NOTIFY: {
        ESP_LOGI(TAG, "Recv OTA NEW BIN Version Notification (NBVN) from 0x%04x", ctx->addr);
        if (length != BLE_MESH_NEW_BIN_VERSION_NOTIFY_LEN) {
            ESP_LOGE(TAG, "Invalid New Bin Version Notification length %d", length);
            return;
        }

        if (ESP_BLE_MESH_ADDR_IS_UNICAST(server->peer_addr) && server->peer_addr != ctx->addr) {
            ESP_LOGW(TAG, "Ignore New Bin Version Notification from 0x%04x", ctx->addr);
            return;
        }

        uint16_t bin_id      = msg[1] << 8 | msg[0];
        uint8_t  version     = msg[2];
        uint8_t  sub_version = msg[3];
        uint8_t  flag        = msg[4];
        uint16_t group_addr  = msg[6] << 8 | msg[5];

        if (!ESP_BLE_MESH_ADDR_IS_GROUP(group_addr)) {
            ESP_LOGW(TAG, "Invalid group address 0x%04x", group_addr);
            return;
        }

        ESP_LOGI(TAG, "Recv bin_id 0x%04x, version %d.%d.%d, flag 0x%02x, group_addr 0x%04x",
            bin_id, version, (sub_version >> 4) & 0x0f, sub_version & 0x0f, flag, group_addr);

        if ((bin_id != server->bin_id) || (version << 8 | sub_version) <= (server->curr_version << 8 | server->curr_sub_version)) {
            ESP_LOGW(TAG, "Current version No need for OTA update, bin_id 0x%04x, version %d.%d.%d",
                server->bin_id, server->curr_version, (server->curr_sub_version >> 4) & 0x0f, server->curr_sub_version & 0x0f);
            return;
        }

        /* Publish the Need OTA Update Notification to the group address */
        ble_mesh_need_ota_update_ntf_t noun = {
            .bin_id      = server->bin_id,
            .version     = server->curr_version,
            .sub_version = server->curr_sub_version,
        };
        ESP_LOGI(TAG, "Sending Need OTA Update NTF (NOUN) to 0x%04x", ctx->addr);
        err = ble_mesh_publish_need_ota_update_ntf(model, ctx->app_idx, ctx->addr, &noun);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to publish Need OTA Update Notification");
            return;
        }

        /* Store New Bin Version info in flash. After OTA update succeeds,
         * initialize ota_client user data with restored values.
         */
        ota_nvs_data.bin_id           = server->bin_id;
        ota_nvs_data.curr_version     = server->curr_version;
        ota_nvs_data.curr_sub_version = server->curr_sub_version;
        ota_nvs_data.next_version     = version;
        ota_nvs_data.next_sub_version = sub_version;
        ota_nvs_data.group_addr       = group_addr;
        ota_nvs_data.flag             = flag;
        err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store ota nvs data, err: 0x%04x", err);
        }
        recv_nbvn_flag = true;
        break;
    }
    case BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START: {
        /* If receiving OTA Update Start from the same device again, this may be
         * caused by failing to receive the Need OTA Update response, and in this
         * situation we need to resend the response.
         */
        ESP_LOGI(TAG, "Recv OTA Update Start (OUS) from 0x%04x", ctx->addr);
        if (length < BLE_MESH_OTA_UPDATE_START_LEN) {
            ESP_LOGE(TAG, "Invalid OTA Update Start length %d", length);
            return;
        }

        if (!recv_nbvn_flag) {
            ESP_LOGE(TAG, "Don't receive NEW BIN Version Notification (NBVN)");
            return;
        }

        if (ESP_BLE_MESH_ADDR_IS_UNICAST(server->peer_addr) && server->peer_addr != ctx->addr) {
            ESP_LOGE(TAG, "Ignore OTA Update Start from 0x%04x", ctx->addr);
            return;
        }

        uint8_t  role = msg[0];
        uint16_t ssid = msg[2] << 8 | msg[1];
        uint16_t pwd  = msg[4] << 8 | msg[3];
        ESP_LOGD(TAG, "role %s, ssid 0x%04x, password 0x%04x", role ? "Board" : "Phone", ssid, pwd);

        ble_mesh_ota_update_status_t ous = {0};

        if (role > BLE_MESH_OTA_ROLE_BOARD) {
            ous.status = BLE_MESH_OTA_UPDATE_STATUS_INVALID_PARAM; /* Invalid parameter */

            ESP_LOGI(TAG, "Sending OTA Update Status (OUS) 0x%02x to 0x%04x", ous.status, ctx->addr);
            err = ble_mesh_send_ota_update_status(model, ctx->net_idx, ctx->app_idx, ctx->addr, &ous);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send OTA Update Status (OUS)");
                return;
            }
            return;
        }

        if (server->peer_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
            /* Store peer_role, peer_addr, ssid, password to flash */
            ota_nvs_data.dev_flag |= BLE_MESH_OTA_NEED_UPDATE;
            ota_nvs_data.peer_role = role;
            ota_nvs_data.peer_addr = ctx->addr;
            ota_nvs_data.ssid      = ssid;
            ota_nvs_data.password  = pwd;
            if (ota_nvs_data.flag & BLE_MESH_STACK_DATA_FROM_URL) {
                ESP_LOGD(TAG, "ota_url: %s", msg + 5);
                ESP_LOGD(TAG, "url_ssid: %s", msg + 5 + 256);
                ESP_LOGD(TAG, "url_pass: %s", msg + 5 + 256 + 32);
                memcpy(ota_nvs_data.ota_url, msg + 5, 256);
                memcpy(ota_nvs_data.url_ssid, msg + 5 + 256, 32);
                memcpy(ota_nvs_data.url_pass, msg + 5 + 256 + 32, 64);
            }
            ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
            err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store ota nvs data, err: 0x%04x", err);
                ous.status = BLE_MESH_OTA_UPDATE_STATUS_STORE_FAIL; /* Failed to store */
            } else {
                ous.status = BLE_MESH_OTA_UPDATE_STATUS_SUCCEED; /* Succeed */

                /* Store peer unicast address */
                server->peer_addr = ctx->addr;
                ESP_LOGD(TAG, "server->peer_addr 0x%04x", server->peer_addr);
            }

            /* Cancel periodic Need OTA Update Notification publication */
            model->pub->period = 0x00;

            ESP_LOGI(TAG, "Sending OTA Update Status (OUS) 0x%02x to 0x%04x", ous.status, ctx->addr);
            err = ble_mesh_send_ota_update_status(model, ctx->net_idx, ctx->app_idx, ctx->addr, &ous);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send OTA Update Status (OUS)");
                return;
            }
            if (ous.status == BLE_MESH_OTA_UPDATE_STATUS_SUCCEED) {
                ESP_LOGD(TAG, "esp_timer_restart, %d", __LINE__);
                esp_timer_stop(g_server_wifi_change_timer);
                esp_timer_start_once(g_server_wifi_change_timer, OTA_SERVER_WIFI_CHANGE_TIMEOUT_US);
            }
        } else if (server->peer_addr == ctx->addr) {
            ESP_LOGI(TAG, "Recv repeat OTA Update Start (OUS) from 0x%04x", ctx->addr);
        } else {
            /* If received OTA Update Start again, cancel the reboot timer.
             * After OTA Update status with status set to "success" is sent
             * successfully, start the reboot timer.
             */
            ESP_LOGD(TAG, "wifi_change_timer_stop, %d", __LINE__);
            esp_timer_stop(g_server_wifi_change_timer);
        }
        break;
    }
    default:
        ESP_LOGE(TAG, "Invalid ota server message, opcode: 0x%06x", opcode);
        break;
    }
}

esp_err_t ble_mesh_ota_server_init(esp_ble_mesh_model_t *model)
{
    ble_mesh_ota_server_data_t *server = model->user_data;

    if (!server) {
        ESP_LOGE(TAG, "No ota server context provided");
        return ESP_ERR_INVALID_ARG;
    }

    esp_timer_create_args_t timer_args = {
        .callback = &server_wifi_change_timeout_cb,
        .name     = "server_wifi_change",
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_server_wifi_change_timer));

    server->model = model;

    return ESP_OK;
}
