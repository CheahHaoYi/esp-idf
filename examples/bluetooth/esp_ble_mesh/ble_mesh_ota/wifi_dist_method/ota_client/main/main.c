/* main.c - Application main entry point */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "main.h"

#define TAG "EXAMPLE"

#ifdef CONFIG_EXAMPLE_WIFI_CRED_FROM_STDIN
char wifi_ssid[WIFI_SSID_MAX_LEN];
char wifi_pw[WIFI_PSWD_MAX_LEN];
#else
char wifi_ssid[] = CONFIG_EXAMPLE_WIFI_SSID;
char wifi_pw[] = CONFIG_EXAMPLE_WIFI_PASSWORD;
#endif

#ifdef CONFIG_EXAMPLE_OTA_INFO_FROM_STDIN
char ota_url[OTA_URL_SIZE];
uint64_t ota_size = 0;
#else 
char ota_url[] = CONFIG_EXAMPLE_OTA_URL;
uint64_t ota_size = CONFIG_EXAMPLE_OTA_SIZE;
#endif

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN];
static nvs_handle_t NVS_HANDLE;
static const char * NVS_KEY = "vendor_client";

static struct example_info_store {
    uint16_t server_addr;   /* Vendor server unicast address */
    uint16_t vnd_tid;       /* TID contained in the vendor message */
    uint16_t num_servers;   /* Number of vendor servers discovered */
} store = {
    .server_addr = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .vnd_tid = 0,
    .num_servers = 0,
};

static struct esp_ble_mesh_key {
    uint16_t net_idx;
    uint16_t app_idx;
    uint8_t  app_key[ESP_BLE_MESH_OCTET16_LEN];
} prov_key;

static esp_ble_mesh_cfg_srv_t config_server = {
    .beacon = ESP_BLE_MESH_BEACON_DISABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = DEFAULT_TTL,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(TRASMIT_COUNT, TRANSMIT_INTERVAL),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(TRASMIT_COUNT, TRANSMIT_INTERVAL),
};

static esp_ble_mesh_client_t config_client;

static const esp_ble_mesh_client_op_pair_t vnd_op_pair[] = {
    { ESP_BLE_MESH_VND_MODEL_OP_SEND, ESP_BLE_MESH_VND_MODEL_OP_STATUS },
    { ESP_BLE_MESH_VND_MODEL_OP_SSID_TRSF, ESP_BLE_MESH_VND_MODEL_OP_STATUS },
    { ESP_BLE_MESH_VND_MODEL_OP_PW_TRSF, ESP_BLE_MESH_VND_MODEL_OP_STATUS },
    { ESP_BLE_MESH_VND_MODEL_OP_OTA_URL_TRSF, ESP_BLE_MESH_VND_MODEL_OP_STATUS },
    { ESP_BLE_MESH_VND_MODEL_OP_OTA_SIZE_TRSF, ESP_BLE_MESH_VND_MODEL_OP_STATUS },
    { ESP_BLE_MESH_VND_MODEL_OP_OTA_PROGRESS, ESP_BLE_MESH_VND_MODEL_OP_STATUS },
    { ESP_BLE_MESH_VND_MODEL_OP_ESPNOW_UPDATE, ESP_BLE_MESH_VND_MODEL_OP_STATUS}
};

static esp_ble_mesh_client_t vendor_client = {
    .op_pair_size = ARRAY_SIZE(vnd_op_pair),
    .op_pair = vnd_op_pair,
};

static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_STATUS, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_OTA_PROGRESS, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_ESPNOW_UPDATE, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, ESP_BLE_MESH_VND_MODEL_ID_CLIENT,
    vnd_op, NULL, &vendor_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(LOC_DESCR, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t provision = {
    .prov_uuid          = dev_uuid,
    .prov_unicast_addr  = PROV_OWN_ADDR,
    .prov_start_address = PROV_START_ADDR,
};

static void mesh_example_info_store(void)
{
    ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &store, sizeof(store));
}

static void mesh_example_info_restore(void)
{
    esp_err_t err = ESP_OK;
    bool exist = false;

    err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &store, sizeof(store), &exist);
    if (err != ESP_OK) {
        return;
    }

    if (exist) {
        ESP_LOGI(TAG, "Restore, server_addr 0x%04x, vnd_tid 0x%04x", store.server_addr, store.vnd_tid);
    }
}

static void example_ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                            esp_ble_mesh_node_t *node,
                                            esp_ble_mesh_model_t *model, uint32_t opcode)
{
    common->opcode = opcode;
    common->model = model;
    common->ctx.net_idx = prov_key.net_idx;
    common->ctx.app_idx = prov_key.app_idx;
    common->ctx.addr = node->unicast_addr;
    common->ctx.send_ttl = MSG_SEND_TTL;
    common->ctx.send_rel = MSG_SEND_REL;
    common->msg_timeout = MSG_TIMEOUT;
    common->msg_role = MSG_ROLE;
}

static esp_err_t prov_complete(uint16_t node_index, const esp_ble_mesh_octet16_t uuid,
                               uint16_t primary_addr, uint8_t element_num, uint16_t net_idx)
{
    esp_err_t err;
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_get_state_t get = {0};
    esp_ble_mesh_node_t *node = NULL;
    char name[10] = {'\0'};

    ESP_LOGI(TAG, "node_index %u, primary_addr 0x%04x, element_num %u, net_idx 0x%03x",
        node_index, primary_addr, element_num, net_idx);
    ESP_LOG_BUFFER_HEX("uuid", uuid, ESP_BLE_MESH_OCTET16_LEN);

    store.server_addr = primary_addr;
    mesh_example_info_store(); /* Store proper mesh example info */

    sprintf(name, "%s%02x", "NODE-", node_index);
    err = esp_ble_mesh_provisioner_set_node_name(node_index, name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set node name");
        return ESP_FAIL;
    }

    node = esp_ble_mesh_provisioner_get_node_with_addr(primary_addr);
    if (node == NULL) {
        ESP_LOGE(TAG, "Failed to get node 0x%04x info", primary_addr);
        return ESP_FAIL;
    }

    example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
    get.comp_data_get.page = COMP_DATA_PAGE_0;
    err = esp_ble_mesh_config_client_get_state(&common, &get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send Config Composition Data Get");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void recv_unprov_adv_pkt(uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN], uint8_t addr[BD_ADDR_LEN],
                                esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                                uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer)
{
    esp_ble_mesh_unprov_dev_add_t add_dev = {
        .addr_type = (uint8_t)addr_type,
        .oob_info = oob_info,
        .bearer = (uint8_t)bearer,
    };
    memcpy(add_dev.addr, addr, BD_ADDR_LEN);
    memcpy(add_dev.uuid, dev_uuid, ESP_BLE_MESH_OCTET16_LEN);


    /* Due to the API esp_ble_mesh_provisioner_set_dev_uuid_match, Provisioner will only
     * use this callback to report the devices, whose device UUID starts with 0xdd & 0xdd,
     * to the application layer.
     */

    ESP_LOG_BUFFER_HEX("Device address", addr, BD_ADDR_LEN);
    ESP_LOGI(TAG, "Address type 0x%02x, adv type 0x%02x", addr_type, adv_type);
    ESP_LOG_BUFFER_HEX("Device UUID", dev_uuid, ESP_BLE_MESH_OCTET16_LEN);
    ESP_LOGI(TAG, "oob info 0x%04x, bearer %s", oob_info, (bearer & ESP_BLE_MESH_PROV_ADV) ? "PB-ADV" : "PB-GATT");
    
    /* Note: If unprovisioned device adv packets have not been received, we should not add
             device with ADD_DEV_START_PROV_NOW_FLAG set. */
    esp_err_t err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev,
            ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning device");
    }
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        mesh_example_info_restore(); /* Restore proper mesh example info */
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT, err_code %d", param->provisioner_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT, err_code %d", param->provisioner_prov_disable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT");
        recv_unprov_adv_pkt(param->provisioner_recv_unprov_adv_pkt.dev_uuid, param->provisioner_recv_unprov_adv_pkt.addr,
                            param->provisioner_recv_unprov_adv_pkt.addr_type, param->provisioner_recv_unprov_adv_pkt.oob_info,
                            param->provisioner_recv_unprov_adv_pkt.adv_type, param->provisioner_recv_unprov_adv_pkt.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT, bearer %s",
            param->provisioner_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT, bearer %s, reason 0x%02x",
            param->provisioner_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", param->provisioner_prov_link_close.reason);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        prov_complete(param->provisioner_prov_complete.node_idx, param->provisioner_prov_complete.device_uuid,
                      param->provisioner_prov_complete.unicast_addr, param->provisioner_prov_complete.element_num,
                      param->provisioner_prov_complete.netkey_idx);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT, err_code %d", param->provisioner_add_unprov_dev_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT, err_code %d", param->provisioner_set_dev_uuid_match_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT, err_code %d", param->provisioner_set_node_name_comp.err_code);
        if (param->provisioner_set_node_name_comp.err_code == 0) {
            const char *name = esp_ble_mesh_provisioner_get_node_name(param->provisioner_set_node_name_comp.node_index);
            if (name) {
                ESP_LOGI(TAG, "Node %d name %s", param->provisioner_set_node_name_comp.node_index, name);
            }
            store.num_servers++;
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT, err_code %d", param->provisioner_add_app_key_comp.err_code);
        if (param->provisioner_add_app_key_comp.err_code == 0) {
            prov_key.app_idx = param->provisioner_add_app_key_comp.app_idx;
            esp_err_t err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, prov_key.app_idx,
                    ESP_BLE_MESH_VND_MODEL_ID_CLIENT, CID_ESP);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to bind AppKey to vendor client");
            }
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT, err_code %d", param->provisioner_bind_app_key_to_model_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT, err_code %d", param->provisioner_store_node_comp_data_comp.err_code);
        break;
    default:
        break;
    }
}

static void example_ble_mesh_parse_node_comp_data(const uint8_t *data, uint16_t length)
{
    uint16_t cid = COMP_DATA_2_OCTET(data, 0);
    uint16_t pid = COMP_DATA_2_OCTET(data, 2);
    uint16_t vid = COMP_DATA_2_OCTET(data, 4);
    uint16_t crpl = COMP_DATA_2_OCTET(data, 6);
    uint16_t feat = COMP_DATA_2_OCTET(data, 8);

    ESP_LOGI(TAG, "********************** Composition Data Start **********************");
    ESP_LOGI(TAG, "* CID 0x%04x, PID 0x%04x, VID 0x%04x, CRPL 0x%04x, Features 0x%04x *", cid, pid, vid, crpl, feat);
    for (uint16_t offset = COMPOSITION_DATA_OFFSET; offset < length; ) {
        uint16_t loc = COMP_DATA_2_OCTET(data, offset);
        uint8_t nums = COMP_DATA_1_OCTET(data, offset + 2);
        uint8_t numv = COMP_DATA_1_OCTET(data, offset + 3);
        ESP_LOGI(TAG, "* Loc 0x%04x, NumS 0x%02x, NumV 0x%02x *", loc, nums, numv);

        offset += 4;

        for (int i = 0; i < nums; i++) {
            uint16_t model_id = COMP_DATA_2_OCTET(data, offset);
            ESP_LOGI(TAG, "* SIG Model ID 0x%04x *", model_id);
            offset += 2;
        }

        for (int i = 0; i < numv; i++) {
            uint16_t company_id = COMP_DATA_2_OCTET(data, offset);
            uint16_t model_id = COMP_DATA_2_OCTET(data, offset + 2);
            ESP_LOGI(TAG, "* Vendor Model ID 0x%04x, Company ID 0x%04x *", model_id, company_id);
            offset += 4;
        }
    }
    ESP_LOGI(TAG, "*********************** Composition Data End ***********************");
}

static void example_ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                                              esp_ble_mesh_cfg_client_cb_param_t *param)
{
    esp_err_t err;
    esp_ble_mesh_node_t *node = NULL;

    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_set_state_t set = {0};

    ESP_LOGI(TAG, "Config client, err_code %d, event %u, addr 0x%04x, opcode 0x%04" PRIx32,
        param->error_code, event, param->params->ctx.addr, param->params->opcode);

    if (param->error_code) {
        ESP_LOGE(TAG, "Send config client message failed, opcode 0x%04" PRIx32, param->params->opcode);
        return;
    }

    node = esp_ble_mesh_provisioner_get_node_with_addr(param->params->ctx.addr);
    if (!node) {
        ESP_LOGE(TAG, "Failed to get node 0x%04x info", param->params->ctx.addr);
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
            ESP_LOG_BUFFER_HEX("Composition data", param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            example_ble_mesh_parse_node_comp_data(param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            err = esp_ble_mesh_provisioner_store_node_comp_data(param->params->ctx.addr,
                param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store node composition data");
                break;
            }

            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set.app_key_add.net_idx = prov_key.net_idx;
            set.app_key_add.app_idx = prov_key.app_idx;
            memcpy(set.app_key_add.app_key, prov_key.app_key, ESP_BLE_MESH_OCTET16_LEN);
            err = esp_ble_mesh_config_client_set_state(&common, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config AppKey Add");
            }
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            set.model_app_bind.element_addr = node->unicast_addr;
            set.model_app_bind.model_app_idx = prov_key.app_idx;
            set.model_app_bind.model_id = ESP_BLE_MESH_VND_MODEL_ID_SERVER;
            set.model_app_bind.company_id = CID_ESP;
            err = esp_ble_mesh_config_client_set_state(&common, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config Model App Bind");
            }
        } else if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            ESP_LOGW(TAG, "%s, Provision and config successfully", __func__);
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_STATUS) {
            ESP_LOG_BUFFER_HEX("Composition data", param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        switch (param->params->opcode) {
        case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET: {
            esp_ble_mesh_cfg_client_get_state_t get = {0};
            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
            get.comp_data_get.page = COMP_DATA_PAGE_0;
            err = esp_ble_mesh_config_client_get_state(&common, &get);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config Composition Data Get");
            }
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set.app_key_add.net_idx = prov_key.net_idx;
            set.app_key_add.app_idx = prov_key.app_idx;
            memcpy(set.app_key_add.app_key, prov_key.app_key, ESP_BLE_MESH_OCTET16_LEN);
            err = esp_ble_mesh_config_client_set_state(&common, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config AppKey Add");
            }
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            set.model_app_bind.element_addr = node->unicast_addr;
            set.model_app_bind.model_app_idx = prov_key.app_idx;
            set.model_app_bind.model_id = ESP_BLE_MESH_VND_MODEL_ID_SERVER;
            set.model_app_bind.company_id = CID_ESP;
            err = esp_ble_mesh_config_client_set_state(&common, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config Model App Bind");
            }
            break;
        default:
            break;
        }
        break;
    default:
        ESP_LOGE(TAG, "Invalid config client event %u", event);
        break;
    }
}

void example_ble_mesh_send_ota_credential(void)
{
    esp_err_t err;

    for (int i = 0; i < store.num_servers; i++) {
        esp_ble_mesh_msg_ctx_t ctx = {
            .net_idx = prov_key.net_idx,
            .app_idx = prov_key.app_idx,
            .addr = PROV_START_ADDR + i,
            .send_ttl = MSG_SEND_TTL,
            .send_rel = MSG_SEND_REL,
        };

        // Send wifi ssid, pw, ota url, firmware size in that order
        err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, ESP_BLE_MESH_VND_MODEL_OP_SSID_TRSF,
            sizeof(wifi_ssid), (uint8_t *)&wifi_ssid, MSG_TIMEOUT, false, MSG_ROLE);
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Failed to send Wifi SSID");
            return;
        }
        
        err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, ESP_BLE_MESH_VND_MODEL_OP_PW_TRSF,
            sizeof(wifi_pw), (uint8_t *)&wifi_pw, MSG_TIMEOUT, false, MSG_ROLE);
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Failed to send Wifi PW");
            return;
        }
        err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, ESP_BLE_MESH_VND_MODEL_OP_OTA_URL_TRSF,
            sizeof(ota_url), (uint8_t *)&ota_url, MSG_TIMEOUT, false, MSG_ROLE);
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Failed to send OTA URL");
            return;
        }
        err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, ESP_BLE_MESH_VND_MODEL_OP_OTA_SIZE_TRSF,
            sizeof(ota_size), (uint8_t *)&ota_size, MSG_TIMEOUT, false, MSG_ROLE);
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Failed to send OTA size");
            return;
        }

        ESP_LOGI(TAG, "Sent OTA credentials");
    }
    mesh_example_info_store(); /* Store proper mesh example info */
}


void example_ble_mesh_send_vendor_message(bool resend) // to remove
{
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint32_t opcode;
    esp_err_t err;

    ctx.net_idx = prov_key.net_idx;
    ctx.app_idx = prov_key.app_idx;
    ctx.addr = store.server_addr;
    ctx.send_ttl = MSG_SEND_TTL;
    ctx.send_rel = MSG_SEND_REL;
    opcode = ESP_BLE_MESH_VND_MODEL_OP_SEND;

    if (resend == false) {
        store.vnd_tid++;
    }

    err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
            sizeof(store.vnd_tid), (uint8_t *)&store.vnd_tid, MSG_TIMEOUT, true, MSG_ROLE);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send vendor message 0x%06" PRIx32, opcode);
        return;
    } else {
        ESP_LOGI(TAG, "Send vendor message 0x%06" PRIx32 ", tid 0x%04x", opcode, store.vnd_tid);
    }

    mesh_example_info_store(); /* Store proper mesh example info */
}

static void example_ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                             esp_ble_mesh_model_cb_param_t *param)
{
    static int64_t start_time;

    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_STATUS) {
            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Recv 0x06%" PRIx32 ", tid 0x%04x, time %lldus",
                param->model_operation.opcode, store.vnd_tid, end_time - start_time);
        } 
        break;
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG, "Failed to send message 0x%06" PRIx32, param->model_send_comp.opcode);
            break;
        }
        start_time = esp_timer_get_time();
        ESP_LOGI(TAG, "Send 0x%06" PRIx32, param->model_send_comp.opcode);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        if (param->client_recv_publish_msg.opcode == ESP_BLE_MESH_VND_MODEL_OP_OTA_PROGRESS) {
            uint64_t ota_size = 0;
            memcpy(&ota_size, param->client_recv_publish_msg.msg, sizeof(ota_size));
            ESP_LOGI(TAG, "Received ota update, OTA size: %" PRIu64, ota_size);
        } else {
            ESP_LOGI(TAG, "Receive publish message 0x%06" PRIx32, param->client_recv_publish_msg.opcode);
        }
        
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Client message 0x%06" PRIx32 " timeout", param->client_send_timeout.opcode);
        example_ble_mesh_send_vendor_message(true);
        break;
    default:
        break;
    }
}

static esp_err_t ble_mesh_init(void)
{
    esp_err_t err;
    uint8_t match[2] = DEVICE_UUID;

    prov_key.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    prov_key.app_idx = APP_KEY_IDX;
    memset(prov_key.app_key, APP_KEY_OCTET, sizeof(prov_key.app_key));

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_client_callback(example_ble_mesh_config_client_cb);
    esp_ble_mesh_register_custom_model_callback(example_ble_mesh_custom_model_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return err;
    }

    err = esp_ble_mesh_client_model_init(&vnd_models[0]);
    if (err) {
        ESP_LOGE(TAG, "Failed to initialize vendor client");
        return err;
    }

    err = esp_ble_mesh_provisioner_set_dev_uuid_match(match, sizeof(match), 0x0, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set matching device uuid");
        return err;
    }

    err = esp_ble_mesh_provisioner_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh provisioner");
        return err;
    }

    err = esp_ble_mesh_provisioner_add_local_app_key(prov_key.app_key, prov_key.net_idx, prov_key.app_idx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add local AppKey");
        return err;
    }

    ESP_LOGI(TAG, "ESP BLE Mesh Provisioner initialized");

    return ESP_OK;
}

// TODO: read OTA info, using temp var
#if CONFIG_EXAMPLE_OTA_INFO_FROM_STDIN
static esp_err_t read_ota_info(void)
{
    example_configure_stdin_stdout();

    ESP_LOGI(TAG, "Please input firmware upgrade image url: ");
    fgets(ota_url, OTA_URL_SIZE, stdin);
    int len = strlen(ota_url);
    
    if (len == 0) {
        ESP_LOGE(TAG, "No url provided");
        return ESP_ERR_INVALID_ARG;
    }
    ota_url[len - 1] = '\0';

    ESP_LOGI(TAG, "Please input firmware upgrade image size: ");
    if (scanf("%" SCNu64, &ota_size) != 1) {
        ESP_LOGE(TAG, "Invalid size provided");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "OTA size: %" PRIu64 ", URL: %s", ota_size, ota_url);
    return ESP_OK;
}
#endif

// TODO: read wifi info
#if CONFIG_EXAMPLE_WIFI_CRED_FROM_STDIN
static esp_err_t read_wifi_info(void)
{
    example_configure_stdin_stdout();

    ESP_LOGI(TAG, "Please input wifi ssid: ");
    fgets(wifi_ssid, WIFI_SSID_MAX_LEN, stdin);
    int len = strlen(wifi_ssid);
    wifi_ssid[len - 1] = '\0';

    ESP_LOGI(TAG, "Please input wifi password: ");
    fgets(wifi_pw, WIFI_PSWD_MAX_LEN, stdin);
    len = strlen(wifi_pw);
    wifi_pw[len - 1] = '\0';

    ESP_LOGI(TAG, "Wifi credentials received: %s , %s ", wifi_ssid, wifi_pw);

    return ESP_OK;
}
#endif

void example_esp_now_send_firmware(void)
{
     // sending only to 1 server device
    esp_ble_mesh_msg_ctx_t ctx = {
            .net_idx = prov_key.net_idx,
            .app_idx = prov_key.app_idx,
            .addr = PROV_START_ADDR,
            .send_ttl = MSG_SEND_TTL,
            .send_rel = MSG_SEND_REL,
    };

    // Ask server to switch to espnow for firmware transfer
    esp_err_t err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, ESP_BLE_MESH_VND_MODEL_OP_ESPNOW_UPDATE,
        sizeof(wifi_ssid), (uint8_t *)&wifi_ssid, MSG_TIMEOUT, false, MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Failed to send Wifi SSID");
        return;
    }
    DELAY(1000);

    err = connection_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW connection");
        return;
    }

    err = ota_firmware_fetch(ota_url);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch firmware");
        return;
    }

    err = ota_firmware_send();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send firmware");
        return;
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    board_init();

    err = bluetooth_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    /* Open nvs namespace for storing/restoring mesh example info */
    err = ble_mesh_nvs_open(&NVS_HANDLE);
    if (err) {
        ESP_LOGE(TAG, "NVS open failed (err %d)", err);
        return;
    }

    ble_mesh_get_dev_uuid(dev_uuid);

    #if CONFIG_EXAMPLE_WIFI_CRED_FROM_STDIN
    err = read_wifi_info();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read wifi info");
        // return;
    }
    #endif

    #if CONFIG_EXAMPLE_OTA_INFO_FROM_STDIN
    err = read_ota_info();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ota info");
        // return;
    }
    #endif

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }

    // Connect to WIFI AP
    err = connect_wifi_ap();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize connection");
        return;
    }
}
