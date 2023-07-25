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

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "esp_ota_ops.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "ble_mesh_example_nvs.h"

#include "ble_mesh_ota_model_common.h"
#include "ble_mesh_ota_model_msg.h"
#include "ble_mesh_ota_client_model.h"
#include "ble_mesh_ota_server_model.h"

#include "ble_mesh_ota_op.h"
#include "ble_mesh_ota_bearer.h"
#include "ble_mesh_ota_utility.h"

#define TAG "ota_util"

extern void user_event(uint16_t event, void *p_arg);

static esp_err_t ota_partition_pre_erase(void)
{
    ENTER_FUNC();
    esp_err_t err = ESP_OK;

    /* Erases next OTA partition to save about 3 seconds during OTA procedure */
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    if (!p) {
        ESP_LOGE(TAG, "Failed to find ota partition");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "partition, type 0x%02x, sub_type 0x%02x, address 0x%08x, size %d",
             p->type, p->subtype, p->address, p->size);

    err = esp_partition_erase_range(p, 0, p->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase ota partition");
        return err;
    }

    /* Erase BLE Mesh OTA partition successfully and set the proper flag */
    ota_nvs_data.dev_flag |= BLE_MESH_OTA_PARTION_ERASED;
    ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
    err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store ota nvs data, err: 0x%04x", err);
    }

    return err;
}

static uint8_t get_ota_nvs_flag(void)
{
    ENTER_FUNC();
    uint8_t need_update, update_done;

    need_update = ota_nvs_data.dev_flag & BLE_MESH_OTA_NEED_UPDATE ? 1 : 0;
    update_done = ota_nvs_data.dev_flag & BLE_MESH_OTA_UPDATE_DONE ? 1 : 0;
    ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);

    switch (need_update + update_done) {
    case 0x0:
        ESP_LOGI(TAG, "need_update & update_done bits are not set");
        return BLE_MESH_OTA_ACT_NONE;
    case 0x01:
        if (need_update) {
            ESP_LOGI(TAG, "need_update bit is set and update_done is not set");
            return BLE_MESH_OTA_ACT_NEED_OTA;
        }

        ESP_LOGI(TAG, "update_done bit is set and need_update is not set");
        if (ota_nvs_data.dev_flag & BLE_MESH_OTA_GET_DEVICE) {
            ESP_LOGI(TAG, "Get ota device and start to update");
            return BLE_MESH_OTA_ACT_OTA_DONE;
        }

        ESP_LOGI(TAG, "Get no ota device, back to mesh node");
        return BLE_MESH_OTA_ACT_NONE;
    case 0x02:
        ESP_LOGI(TAG, "need_update & update_done bits are both set, clear need_update bit");
        /* May happens when the device has been updated and it restarts unexpectedly
         * just before the "BLE_MESH_OTA_NEED_UPDATE" bit is cleared.
         */
        ESP_LOGI(TAG, "Clear need_update bit");
        ota_nvs_data.dev_flag &= ~BLE_MESH_OTA_NEED_UPDATE;
        ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
        esp_err_t err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store ota nvs data, err: 0x%04x", err);
            return BLE_MESH_OTA_ACT_INVALID;
        }

        if (ota_nvs_data.dev_flag & BLE_MESH_OTA_GET_DEVICE) {
            ESP_LOGI(TAG, "Get ota device and start to update");
            return BLE_MESH_OTA_ACT_OTA_DONE;
        }

        ESP_LOGI(TAG, "Get no ota device, back to mesh node");
        return BLE_MESH_OTA_ACT_NONE;
    default:
        return BLE_MESH_OTA_ACT_INVALID;
    }
}

#define OTA_RECV_COM_RESTART_TIMEOUT_US (30 * 1000 * 1000)  /* 30s */

static esp_timer_handle_t g_ota_change_timer = NULL;
static bool g_timer_start                    = false;

static void ota_change_timeout_cb(void *arg)
{
    ENTER_FUNC();
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "ota change timeout cb");
    if (ota_nvs_data.dev_flag & BLE_MESH_OTA_UPDATE_DONE) {
        /* Clear OTA Done Flag. */
        ESP_LOGD(TAG, "Clear OTA Done Flag");
        ota_nvs_data.dev_flag &= ~BLE_MESH_OTA_UPDATE_DONE;
        ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
        err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store ota nvs data, err: 0x%04x", err);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(5 * 1000));
    esp_restart();
}

static esp_err_t ble_mesh_ota_fail_cb(uint8_t ota_op)
{
    ENTER_FUNC();
    ESP_LOGE(TAG, "ota fail cb OTA operation \"%s\"", ota_op == BLE_MESH_OTA_OP_SEG_RECV ? "RECV" : "SEND");
    if (ota_op == BLE_MESH_OTA_OP_SEG_RECV) {
        ESP_LOGE(TAG, "BLE_MESH_OTA_OP_SEG_RECV, Failed");
        user_event(EVT_OTA_RECV_FAILED, NULL);
        ble_mesh_ota_server_clean();
        esp_restart();
    } else if (ota_op == BLE_MESH_OTA_OP_SEG_SEND) {
        ESP_LOGE(TAG, "BLE_MESH_OTA_OP_SEG_SEND, Failed");
        user_event(EVT_OTA_SEND_FAILED, NULL);
        esp_restart();
    }

    return ESP_OK;
}

static esp_err_t ble_mesh_ota_complete_cb(uint8_t ota_op)
{
    ENTER_FUNC();
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "ota complete cb OTA operation \"%s\"", ota_op == BLE_MESH_OTA_OP_SEG_RECV ? "RECV" : "SEND");

    err = ble_mesh_nvs_restore(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t), NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore ota nvs data, err: 0x%04x", err);
        return err;
    }

    if (ota_op == BLE_MESH_OTA_OP_SEG_RECV) {
        ESP_LOGI(TAG, "BLE_MESH_OTA_OP_SEG_RECV, complete");
        user_event(EVT_OTA_RECV_SUCCESS, NULL);
        ota_nvs_data.dev_flag &= ~BLE_MESH_OTA_NEED_UPDATE;
        ota_nvs_data.dev_flag |= BLE_MESH_OTA_UPDATE_DONE; // avoid except reboot
    } else if (ota_op == BLE_MESH_OTA_OP_SEG_SEND) {
        ESP_LOGI(TAG, "BLE_MESH_OTA_OP_SEG_SEND, complete");
        user_event(EVT_OTA_SEND_SUCCESS, NULL);
        ota_nvs_data.dev_flag &= ~BLE_MESH_OTA_GET_DEVICE;
    }
    ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
    err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store ota nvs data, err: 0x%04x", err);
        return err;
    }

    if (ota_op == BLE_MESH_OTA_OP_SEG_RECV) {
        /* If the device has been upgraded, initialize the ota client user data. */
        ota_client_data.bin_id      = ota_nvs_data.bin_id;
        ota_client_data.version     = ota_nvs_data.next_version;
        ota_client_data.sub_version = ota_nvs_data.next_sub_version;
        ota_client_data.group_addr  = ota_nvs_data.group_addr;

        for (size_t i = 0; i < 3; i++) {
            /* Publish the New Bin Version Notification to the group address */
            ble_mesh_new_bin_versino_ntf_t nbvn = {
                .bin_id      = ota_nvs_data.bin_id,
                .version     = ota_nvs_data.next_version,
                .sub_version = ota_nvs_data.next_sub_version,
                .flags       = BLE_MESH_STACK_DATA_NO_ERASE, // flag may be contained BLE_MESH_STACK_DATA_FROM_URL
                .group_addr  = ota_nvs_data.group_addr,
            };
            ESP_LOGI(TAG, "Sending NEW Bin Version NTF %d times", i);
            ble_mesh_send_new_bin_versin_ntf(esp_ble_mesh_find_vendor_model(esp_ble_mesh_find_element(esp_ble_mesh_get_primary_element_address()),
                                             CID_ESP, BLE_MESH_VND_MODEL_ID_OTA_CLIENT), 0x0000, BLE_MESH_BIN_ID_LIGHT_APP_IDX, BLE_MESH_ADDR_ALL_NODES, &nbvn);
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        if (!(ota_nvs_data.flag & BLE_MESH_STACK_DATA_FROM_URL)) {
            ESP_LOGD(TAG, "esp_ota_set_boot_partition");
            esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
        }

        esp_timer_create_args_t timer_args = {
            .callback = &ota_change_timeout_cb,
            .name     = "ota_change_timer",
        };

        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_ota_change_timer));
        esp_timer_start_once(g_ota_change_timer, OTA_RECV_COM_RESTART_TIMEOUT_US);
        g_timer_start = true;
    } else if (ota_op == BLE_MESH_OTA_OP_SEG_SEND) {
        /* Publish the OTA Completed Notification to the group address */
        // ble_mesh_ota_server_data_t *server = esp_ble_mesh_find_vendor_model(esp_ble_mesh_find_element(esp_ble_mesh_get_primary_element_address()),
        //      CID_ESP, BLE_MESH_VND_MODEL_ID_OTA_SERVER).user_data;
        // ble_mesh_ota_completed_ntf_t ouc = {
        //     .bin_id      = server->bin_id,
        //     .version     = server->curr_version,
        //     .sub_version = server->curr_sub_version,
        // };
        // ESP_LOGW(TAG, "Sending OTA Completed NTF");
        // err = ble_mesh_publish_ota_completed_ntf(&esp_ble_mesh_find_vendor_model(esp_ble_mesh_find_element(esp_ble_mesh_get_primary_element_address()),
        //      CID_ESP, BLE_MESH_VND_MODEL_ID_OTA_SERVER), BLE_MESH_BIN_ID_LIGHT_APP_IDX, BLE_MESH_ADDR_ALL_NODES, &ouc);

        ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
        if (ota_nvs_data.dev_flag & BLE_MESH_OTA_UPDATE_DONE) {
            /* Clear OTA Done Flag. */
            ESP_LOGD(TAG, "Clear OTA Done Flag");
            ota_nvs_data.dev_flag &= ~BLE_MESH_OTA_UPDATE_DONE;
            ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
            err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store ota nvs data, err: 0x%04x", err);
            }
        }

        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_deinit());
        if (!g_timer_start) {
            vTaskDelay(pdMS_TO_TICKS(10 * 1000));
            esp_restart();
        }
    }

    return err;
}

esp_err_t ble_mesh_ota_start(void)
{
    ENTER_FUNC();
    if (g_timer_start) {
        g_timer_start = false;
        esp_timer_stop(g_ota_change_timer);
    }

    if (!(ota_nvs_data.dev_flag & BLE_MESH_OTA_PARTION_ERASED)) {
        // if (ota_partition_pre_erase() != ESP_OK) {
        //     return ESP_FAIL;
        // }
    }

    if (ota_nvs_data.flag & BLE_MESH_STACK_DATA_ERASE) {
        /* TODO: erase mesh stack data */
    }

    uint8_t action = get_ota_nvs_flag();
    // action = BLE_MESH_OTA_ACT_OTA_DONE;
    // action = BLE_MESH_OTA_ACT_NEED_OTA;
    // ota_nvs_data.peer_role = BLE_MESH_OTA_ROLE_BOARD;

    if (action != BLE_MESH_OTA_ACT_NONE) {
        ble_mesh_ota_op_init_t ota_init = {0};

        if (action == BLE_MESH_OTA_ACT_NEED_OTA) {
            ESP_LOGI(TAG, "BLE_MESH_OTA_ACT_NEED_OTA, %d", __LINE__);
            user_event(EVT_OTA_RECV_START, NULL);
            ota_init.ota_op  = BLE_MESH_OTA_OP_SEG_RECV;
            ota_init.max_dev = BLE_MESH_OTA_MAX_UPGRADE_DEV_NUM;
        } else if (action == BLE_MESH_OTA_ACT_OTA_DONE) {
            ESP_LOGI(TAG, "BLE_MESH_OTA_ACT_OTA_DONE, %d", __LINE__);
            user_event(EVT_OTA_SEND_START, NULL);
            ota_init.ota_op         = BLE_MESH_OTA_OP_SEG_SEND;
            ota_init.max_dev        = BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM;
            ota_init.ota_partition  = esp_ota_get_next_update_partition(NULL);
        }
        ota_init.ota_fail_cb        = ble_mesh_ota_fail_cb;
        ota_init.ota_complete_cb    = ble_mesh_ota_complete_cb;
        ota_init.ota_bearer_post_cb = ble_mesh_ota_wifi_task_post;
        if (ble_mesh_ota_op_init_post(&ota_init) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    switch (action) {
    case BLE_MESH_OTA_ACT_NONE:
        ESP_LOGI(TAG, "No ota action to perform, back to mesh node");
        esp_ble_mesh_proxy_gatt_enable();
        break;
    case BLE_MESH_OTA_ACT_NEED_OTA:
    case BLE_MESH_OTA_ACT_OTA_DONE: {
        ble_mesh_ota_wifi_init_t wifi_init = {0};

        if (action == BLE_MESH_OTA_ACT_NEED_OTA) {
            wifi_init.ota_done = false;
            if (ota_nvs_data.flag & BLE_MESH_STACK_DATA_FROM_URL) {
                ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_STA_FROM_URL, %d", __LINE__);
                wifi_init.max_conn  = 1;
                wifi_init.wifi_role = BLE_MESH_OTA_WIFI_STA_FROM_URL;
                memcpy(wifi_init.url_ssid, ota_nvs_data.url_ssid, 32);
                memcpy(wifi_init.url_pass, ota_nvs_data.url_pass, 64);
                memcpy(wifi_init.ota_url, ota_nvs_data.ota_url, 256);
            } else {
                if (ota_nvs_data.peer_role == BLE_MESH_OTA_ROLE_PHONE) {
                    ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_AP, %d", __LINE__);
                    wifi_init.wifi_role = BLE_MESH_OTA_WIFI_AP;
                } else {
                    ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_STA, %d", __LINE__);
                    wifi_init.wifi_role = BLE_MESH_OTA_WIFI_STA;
                }
                wifi_init.max_conn = 1;
                memcpy(wifi_init.ssid, &ota_nvs_data.ssid, sizeof(ota_nvs_data.ssid));
                memcpy(wifi_init.ssid + 2, &ota_nvs_data.peer_addr, sizeof(ota_nvs_data.peer_addr));
                memcpy(wifi_init.password, &ota_nvs_data.password, sizeof(ota_nvs_data.password));
                memcpy(wifi_init.password + 2, &ota_nvs_data.ssid, sizeof(ota_nvs_data.ssid));
                memcpy(wifi_init.password + 4, &ota_nvs_data.peer_addr, sizeof(ota_nvs_data.peer_addr));
                memcpy(wifi_init.password + 6, &ota_nvs_data.password, sizeof(ota_nvs_data.password));
            }
        } else if (action == BLE_MESH_OTA_ACT_OTA_DONE) {
            ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_AP, %d", __LINE__);
            wifi_init.ota_done  = true;
            wifi_init.wifi_role = BLE_MESH_OTA_WIFI_AP;
            wifi_init.max_conn  = BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM;
            memcpy(wifi_init.ssid, &ota_nvs_data.ssid, sizeof(ota_nvs_data.ssid));
            memcpy(wifi_init.ssid + 2, &ota_nvs_data.own_addr, sizeof(ota_nvs_data.own_addr));
            memcpy(wifi_init.password, &ota_nvs_data.password, sizeof(ota_nvs_data.password));
            memcpy(wifi_init.password + 2, &ota_nvs_data.ssid, sizeof(ota_nvs_data.ssid));
            memcpy(wifi_init.password + 4, &ota_nvs_data.own_addr, sizeof(ota_nvs_data.own_addr));
            memcpy(wifi_init.password + 6, &ota_nvs_data.password, sizeof(ota_nvs_data.password));
        }
        if (ble_mesh_ota_bearer_init(&wifi_init) != ESP_OK) {
            return ESP_FAIL;
        }
        break;
    }
    default:
        ESP_LOGE(TAG, "Unknown ota action 0x%02x", action);
        return ESP_FAIL;
    }

    return ESP_OK;
}
