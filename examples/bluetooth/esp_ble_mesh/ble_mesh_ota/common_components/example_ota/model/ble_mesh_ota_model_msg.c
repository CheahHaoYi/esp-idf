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
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_networking_api.h"

#include "ble_mesh_ota_model_common.h"
#include "ble_mesh_ota_model_msg.h"

#define TAG "ota_msg"

esp_err_t ble_mesh_send_new_bin_versin_ntf(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_new_bin_versino_ntf_t *ntf)
{
    uint16_t length = 0;
    uint8_t msg[BLE_MESH_NEW_BIN_VERSION_NOTIFY_LEN] = {0};

    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx  = net_idx,
        .app_idx  = app_idx,
        .addr     = dst,
        .send_rel = false,
        .send_ttl = OTA_MSG_TTL,
    };

    ESP_LOGD(TAG, "net_idx: %04x, app_idx: %04x", ctx.net_idx, ctx.app_idx);

    if (!model || !ntf) {
        ESP_LOGE(TAG, "%s, Invalid argument", __FUNCTION__);
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(msg + length, &ntf->bin_id, sizeof(ntf->bin_id));
    length += sizeof(ntf->bin_id);
    memcpy(msg + length, &ntf->version, sizeof(ntf->version));
    length += sizeof(ntf->version);
    memcpy(msg + length, &ntf->sub_version, sizeof(ntf->sub_version));
    length += sizeof(ntf->sub_version);
    memcpy(msg + length, &ntf->flags, sizeof(ntf->flags));
    length += sizeof(ntf->flags);
    memcpy(msg + length, &ntf->group_addr, sizeof(ntf->group_addr));
    length += sizeof(ntf->group_addr);

    return esp_ble_mesh_client_model_send_msg(model, &ctx, BLE_MESH_VND_MODEL_OP_NEW_BIN_VERSION_NOTIFY, length, msg, OTA_MSG_TIMEOUT, false, ROLE_NODE);
}

esp_err_t ble_mesh_publish_need_ota_update_ntf(esp_ble_mesh_model_t *model, uint16_t app_idx,
        uint16_t dst, ble_mesh_need_ota_update_ntf_t *ntf)
{
    uint16_t length = 0;
    uint8_t msg[BLE_MESH_NEED_OTA_UPDATE_NOTIFY_LEN] = {0};

    if (!model || !ntf) {
        ESP_LOGE(TAG, "%s, Invalid argument", __FUNCTION__);
        return ESP_ERR_INVALID_ARG;
    }

    /* Prepare Model Publication State */
    model->pub->publish_addr = dst;
    model->pub->app_idx      = app_idx;
    model->pub->ttl          = OTA_MSG_TTL;
    model->pub->period       = 0x01 << 6 | 0x0a;  /* 0x01: 1 second step resolution; 0x0a: 10 steps */
    model->pub->dev_role     = ROLE_NODE;
    model->pub->count        = OTA_MSG_CNT;

    memcpy(msg + length, &ntf->bin_id, sizeof(ntf->bin_id));
    length += sizeof(ntf->bin_id);
    memcpy(msg + length, &ntf->version, sizeof(ntf->version));
    length += sizeof(ntf->version);
    memcpy(msg + length, &ntf->sub_version, sizeof(ntf->sub_version));
    length += sizeof(ntf->sub_version);

    return esp_ble_mesh_model_publish(model, BLE_MESH_VND_MODEL_OP_NEED_OTA_UPDATE_NOTIFY, length, msg, ROLE_NODE);
}

esp_err_t ble_mesh_send_current_version_status(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_ota_current_version_status_t *ocvs)
{
    uint16_t length = 0;
    uint8_t msg[BLE_MESH_NEED_OTA_UPDATE_NOTIFY_LEN] = {0};

    if (!model || !ocvs) {
        ESP_LOGE(TAG, "%s, Invalid argument", __FUNCTION__);
        return ESP_ERR_INVALID_ARG;
    }

    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx  = net_idx,
        .app_idx  = app_idx,
        .addr     = dst,
        .send_rel = false,
        .send_ttl = OTA_MSG_TTL,
    };

    memcpy(msg + length, &ocvs->bin_id, sizeof(ocvs->bin_id));
    length += sizeof(ocvs->bin_id);
    memcpy(msg + length, &ocvs->version, sizeof(ocvs->version));
    length += sizeof(ocvs->version);
    memcpy(msg + length, &ocvs->sub_version, sizeof(ocvs->sub_version));
    length += sizeof(ocvs->sub_version);

    return esp_ble_mesh_server_model_send_msg(model, &ctx, BLE_MESH_VND_MODEL_OP_CURRENT_VERSION_STATUS, length, msg);
}

esp_err_t ble_mesh_send_need_ota_update_ntf(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_need_ota_update_ntf_t *ntf)
{
    uint16_t length = 0;
    uint8_t msg[BLE_MESH_NEED_OTA_UPDATE_NOTIFY_LEN] = {0};

    if (!model || !ntf) {
        ESP_LOGE(TAG, "%s, Invalid argument", __FUNCTION__);
        return ESP_ERR_INVALID_ARG;
    }

    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx  = net_idx,
        .app_idx  = app_idx,
        .addr     = dst,
        .send_rel = false,
        .send_ttl = OTA_MSG_TTL,
    };

    memcpy(msg + length, &ntf->bin_id, sizeof(ntf->bin_id));
    length += sizeof(ntf->bin_id);
    memcpy(msg + length, &ntf->version, sizeof(ntf->version));
    length += sizeof(ntf->version);
    memcpy(msg + length, &ntf->sub_version, sizeof(ntf->sub_version));
    length += sizeof(ntf->sub_version);

    return esp_ble_mesh_server_model_send_msg(model, &ctx, BLE_MESH_VND_MODEL_OP_NEED_OTA_UPDATE_NOTIFY, length, msg);
}

esp_err_t ble_mesh_send_ota_update_status(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_ota_update_status_t *status)
{
    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx  = net_idx,
        .app_idx  = app_idx,
        .addr     = dst,
        .send_rel = false,
        .send_ttl = OTA_MSG_TTL,
    };

    if (!model || !status) {
        ESP_LOGE(TAG, "%s, Invalid argument", __FUNCTION__);
        return ESP_ERR_INVALID_ARG;
    }

    return esp_ble_mesh_server_model_send_msg(model, &ctx, BLE_MESH_VND_MODEL_OP_OTA_UPDATE_STATUS, sizeof(status->status), &status->status);
}

esp_err_t ble_mesh_send_ota_update_start(esp_ble_mesh_model_t *model, uint16_t net_idx, uint16_t app_idx,
        uint16_t dst, ble_mesh_ota_update_start_t *start, bool contain_url)
{
    uint16_t length = 0;

    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx  = net_idx,
        .app_idx  = app_idx,
        .addr     = dst,
        .send_rel = false,
        .send_ttl = OTA_MSG_TTL,
    };

    if (!model || !start) {
        ESP_LOGE(TAG, "%s, Invalid argument", __FUNCTION__);
        return ESP_ERR_INVALID_ARG;
    }

    if (contain_url) {
        uint8_t msg[BLE_MESH_OTA_UPDATE_START_LEN + OTA_URL_MAX_LENGTH + OTA_SSID_MAX_LENGTH + OTA_PASS_MAX_LENGTH] = {0};
        memcpy(msg + length, &start->role, sizeof(start->role));
        length += sizeof(start->role);
        memcpy(msg + length, &start->ssid, sizeof(start->ssid));
        length += sizeof(start->ssid);
        memcpy(msg + length, &start->password, sizeof(start->password));
        length += sizeof(start->password);
        memcpy(msg + length, start->ota_url, OTA_URL_MAX_LENGTH);
        length += OTA_URL_MAX_LENGTH;
        memcpy(msg + length, start->url_ssid, OTA_SSID_MAX_LENGTH);
        length += OTA_SSID_MAX_LENGTH;
        memcpy(msg + length, start->url_pass, OTA_PASS_MAX_LENGTH);
        length += OTA_PASS_MAX_LENGTH;
        ESP_LOGD(TAG, "length: %d, ota_url: %d", length, strlen(start->ota_url) + 1);
        return esp_ble_mesh_client_model_send_msg(model, &ctx, BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START, 
            BLE_MESH_OTA_UPDATE_START_LEN + OTA_URL_MAX_LENGTH + OTA_SSID_MAX_LENGTH + OTA_PASS_MAX_LENGTH, msg, OTA_MSG_TIMEOUT, true, ROLE_NODE);
    } else {
        uint8_t msg[BLE_MESH_OTA_UPDATE_START_LEN] = {0};
        memcpy(msg + length, &start->role, sizeof(start->role));
        length += sizeof(start->role);
        memcpy(msg + length, &start->ssid, sizeof(start->ssid));
        length += sizeof(start->ssid);
        memcpy(msg + length, &start->password, sizeof(start->password));
        length += sizeof(start->password);
        return esp_ble_mesh_client_model_send_msg(model, &ctx, BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START, 
            BLE_MESH_OTA_UPDATE_START_LEN, msg, OTA_MSG_TIMEOUT, true, ROLE_NODE);
    }
}

esp_err_t ble_mesh_publish_ota_completed_ntf(esp_ble_mesh_model_t *model, uint16_t app_idx,
        uint16_t dst, ble_mesh_ota_completed_ntf_t *ntf)
{
    uint16_t length = 0;
    uint8_t msg[BLE_MESH_OTA_COMPLETE_NOTIFY_LEN] = {0};

    if (!model || !ntf) {
        ESP_LOGE(TAG, "%s, Invalid argument", __FUNCTION__);
        return ESP_ERR_INVALID_ARG;
    }

    /* Prepare Model Publication State */
    model->pub->publish_addr = dst;
    model->pub->app_idx      = app_idx;
    model->pub->ttl          = OTA_MSG_TTL;
    model->pub->period       = 0x01 << 6 | 0x0a;  /* 0x01: 1 second step resolution; 0x0a: 10 steps */
    model->pub->dev_role     = ROLE_NODE;
    model->pub->count        = OTA_MSG_CNT;

    memcpy(msg + length, &ntf->bin_id, sizeof(ntf->bin_id));
    length += sizeof(ntf->bin_id);
    memcpy(msg + length, &ntf->version, sizeof(ntf->version));
    length += sizeof(ntf->version);
    memcpy(msg + length, &ntf->sub_version, sizeof(ntf->sub_version));
    length += sizeof(ntf->sub_version);

    return esp_ble_mesh_model_publish(model, BLE_MESH_VND_MODEL_OP_OTA_COMPLETED_NOTIFY, length, msg, ROLE_NODE);
}
