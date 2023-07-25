/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
/* C Include */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "errno.h"

/* Specific-ESP Include */
#include "esp_log.h"
#include "nvs_flash.h"

/* FreeRTOS Include */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

/* Bluetooth Include */
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"

/* BLE Mesh Include */
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

/* BLE Mesh Fast Provision Include */
#include "ble_mesh_fast_prov_operation.h"
#include "ble_mesh_fast_prov_client_model.h"
#include "ble_mesh_fast_prov_server_model.h"

/* BLE Mesh Common Component Include */
#include "ble_mesh_example_nvs.h"
#include "ble_mesh_example_init.h"

/* BLE Mesh OTA Include */
#include "ble_mesh_ota_utility.h"
#include "ble_mesh_ota_model_common.h"
#include "ble_mesh_ota_model_msg.h"
#include "ble_mesh_ota_client_model.h"
#include "ble_mesh_ota_server_model.h"

/* BLE Mesh App Utils Include */
#include "app_utils.h"
#include "board.h"

static const char *TAG = "ble-mesh-app";

nvs_handle_t NVS_HANDLE;

extern bt_mesh_atomic_t fast_prov_cli_flags;
extern struct k_delayed_work send_self_prov_node_addr_timer;

elem_state_t g_elem_state[LIGHT_MESH_ELEM_STATE_COUNT] = {0};

/* Different AppKey for different bin id */
static struct ble_mesh_appkey {
    uint8_t  key[16];
    uint16_t net_idx;
    uint16_t app_idx;
} app_keys[] = {
    [BLE_MESH_BIN_ID_LIGHT - 1] = { { 0x02, 0xE5, 0xE5, 0x02, 0x02, 0xE5, 0xE5, 0x02, 0x02, 0xE5, 0xE5, 0x02, 0x02, 0xE5, 0x01, 0x00 },
        0x0000, BLE_MESH_BIN_ID_LIGHT_APP_IDX},
    [BLE_MESH_BIN_ID_SENSOR - 1] = { { 0x02, 0xE5, 0xE5, 0x02, 0x02, 0xE5, 0xE5, 0x02, 0x02, 0xE5, 0xE5, 0x02, 0x02, 0xE5, 0x02, 0x00 },
        0x0000, BLE_MESH_BIN_ID_SENSOR_APP_IDX},
};

static const esp_ble_mesh_client_op_pair_t ota_cli_op_pair[] = {
    { BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START, BLE_MESH_VND_MODEL_OP_OTA_UPDATE_STATUS },
};

static esp_ble_mesh_model_op_t ota_client_op[] = {
    ESP_BLE_MESH_MODEL_OP(BLE_MESH_VND_MODEL_OP_NEED_OTA_UPDATE_NOTIFY, 4),
    ESP_BLE_MESH_MODEL_OP(BLE_MESH_VND_MODEL_OP_OTA_UPDATE_STATUS,      1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_client_t ota_client = {
    .op_pair      = ota_cli_op_pair,
    .op_pair_size = ARRAY_SIZE(ota_cli_op_pair),
};

static esp_ble_mesh_model_op_t ota_server_op[] = {
    ESP_BLE_MESH_MODEL_OP(BLE_MESH_VND_MODEL_OP_NEW_BIN_VERSION_NOTIFY, 7),
    ESP_BLE_MESH_MODEL_OP(BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START,       5),
    ESP_BLE_MESH_MODEL_OP(BLE_MESH_VND_MODEL_OP_GET_CURRENT_VERSION,    0),
    ESP_BLE_MESH_MODEL_OP_END,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(ota_server_pub, 3 + 6, ROLE_NODE);
static ble_mesh_ota_server_data_t ota_server = {
    .bin_id           = BLE_MESH_BIN_ID_LIGHT,
    .curr_version     = VERSION_0,
    .curr_sub_version = SUB_VERSION_MSB_4 << 4 | SUB_VERSION_LSB_5,
    .peer_addr        = ESP_BLE_MESH_ADDR_UNASSIGNED,
};

static bool    prov_start     = false;
static uint8_t prov_start_num = 0x00;
static uint8_t dev_uuid[16]   = {0xFF, 0xFF};

static const esp_ble_mesh_client_op_pair_t fast_prov_cli_op_pair[] = {
    {ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_SET,      ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_STATUS     },
    {ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_ADD,   ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_STATUS  },
    {ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR,     ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_ACK   },
    {ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_GET, ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_STATUS},
};

/* Configuration Client Model user_data */
static esp_ble_mesh_client_t config_client;

/* Configuration Server related context */
static esp_ble_mesh_cfg_srv_t config_server = {
    .relay  = ESP_BLE_MESH_RELAY_ENABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
    /* 2 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(1, 20),
    /* 3 transmissions with 10ms interval */
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 10),
};

/* Fast Prov Client Model user_data */
static esp_ble_mesh_client_t fast_prov_client = {
    .op_pair      = fast_prov_cli_op_pair,
    .op_pair_size = ARRAY_SIZE(fast_prov_cli_op_pair),
};

/* Fast Prov Server Model user_data */
static example_fast_prov_server_t fast_prov_server = {
    .primary_role   = false,
    .max_node_num   = 6,
    .prov_node_cnt  = 0x0,
    .unicast_min    = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .unicast_max    = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .unicast_cur    = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .unicast_step   = 0x0,
    .flags          = 0x0,
    .iv_index       = 0x0,
    .net_idx        = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx        = ESP_BLE_MESH_KEY_UNUSED,
    .group_addr     = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .prim_prov_addr = ESP_BLE_MESH_ADDR_UNASSIGNED,
    .match_len      = 0x0,
    .pend_act       = FAST_PROV_ACT_NONE,
    .state          = STATE_IDLE,
};

static uint8_t test_ids[1] = {0x00};

/** ESP BLE Mesh Health Server Model Context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(health_pub, 2 + 11, ROLE_NODE);
static esp_ble_mesh_health_srv_t health_server = {
    .health_test.id_count = 1,
    .health_test.test_ids = test_ids,
};

/* Generic OnOff Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_pub, 2 + 3, ROLE_FAST_PROV);
static esp_ble_mesh_gen_onoff_srv_t onoff_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

/* Generic Default Transition Time Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(def_trans_time_pub, 2 + 1, ROLE_NODE);
static esp_ble_mesh_gen_def_trans_time_srv_t def_trans_time_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

/* Generic Power OnOff state related context */
static esp_ble_mesh_gen_onpowerup_state_t onpowerup_state = {0};

/* Generic Power OnOff Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(power_onoff_pub, 2 + 1, ROLE_NODE);
static esp_ble_mesh_gen_power_onoff_srv_t power_onoff_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &onpowerup_state,
};

/* Generic Power OnOff Setup Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(power_onoff_setup_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_gen_power_onoff_setup_srv_t power_onoff_setup_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &onpowerup_state,
};

/* Generic Level Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(level_pub_0, 2 + 5, ROLE_NODE);
static esp_ble_mesh_gen_level_srv_t level_server_0 = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

/* Light Lightness state related context */
static esp_ble_mesh_light_lightness_state_t lightness_state = {0};

/* Light Lightness Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(lightness_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_light_lightness_srv_t lightness_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &lightness_state,
};

/* Light Lightness Setup Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(lightness_setup_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_light_lightness_setup_srv_t lightness_setup_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &lightness_state,
};

/* Light HSL state related context */
static esp_ble_mesh_light_hsl_state_t hsl_state = {0};

/* Light HSL Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_pub, 2 + 9, ROLE_NODE);
static esp_ble_mesh_light_hsl_srv_t hsl_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &hsl_state,
};

/* Light HSL Setup Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_setup_pub, 2 + 9, ROLE_NODE);
static esp_ble_mesh_light_hsl_setup_srv_t hsl_setup_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &hsl_state,
};

/* Light CTL state related context */
static esp_ble_mesh_light_ctl_state_t ctl_state = {
    .temperature_range_min = 800,
    .temperature_range_max = 900,
};

/* Light CTL Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(ctl_pub, 2 + 9, ROLE_NODE);
static esp_ble_mesh_light_ctl_srv_t ctl_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &ctl_state,
};

/* Light CTL Setup Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(ctl_setup_pub, 2 + 6, ROLE_NODE);
static esp_ble_mesh_light_ctl_setup_srv_t ctl_setup_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &ctl_state,
};

/* Generic Level Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(level_pub_1, 2 + 5, ROLE_NODE);
static esp_ble_mesh_gen_level_srv_t level_server_1 = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

/* Light HSL Hue Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_hue_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_light_hsl_hue_srv_t hsl_hue_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &hsl_state,
};

/* Generic Level Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(level_pub_2, 2 + 5, ROLE_NODE);
static esp_ble_mesh_gen_level_srv_t level_server_2 = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

/* Light HSL Saturation Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_saturation_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_light_hsl_sat_srv_t hsl_saturation_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &hsl_state,
};

/* Generic Level Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(level_pub_3, 2 + 5, ROLE_NODE);
static esp_ble_mesh_gen_level_srv_t level_server_3 = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

/* Light CTL Temperature Server related context */
ESP_BLE_MESH_MODEL_PUB_DEFINE(ctl_temperature_pub, 2 + 9, ROLE_NODE);
static esp_ble_mesh_light_ctl_temp_srv_t ctl_temperature_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .state                 = &ctl_state,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&health_server, &health_pub),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub, &onoff_server),
    ESP_BLE_MESH_MODEL_GEN_DEF_TRANS_TIME_SRV(&def_trans_time_pub, &def_trans_time_server),
    ESP_BLE_MESH_MODEL_GEN_POWER_ONOFF_SRV(&power_onoff_pub, &power_onoff_server),
    ESP_BLE_MESH_MODEL_GEN_POWER_ONOFF_SETUP_SRV(&power_onoff_setup_pub, &power_onoff_setup_server),
    ESP_BLE_MESH_MODEL_GEN_LEVEL_SRV(&level_pub_0, &level_server_0),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SRV(&lightness_pub, &lightness_server),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SETUP_SRV(&lightness_setup_pub, &lightness_setup_server),
    ESP_BLE_MESH_MODEL_LIGHT_HSL_SRV(&hsl_pub, &hsl_server),
    ESP_BLE_MESH_MODEL_LIGHT_HSL_SETUP_SRV(&hsl_setup_pub, &hsl_setup_server),
    ESP_BLE_MESH_MODEL_LIGHT_CTL_SRV(&ctl_pub, &ctl_server),
    ESP_BLE_MESH_MODEL_LIGHT_CTL_SETUP_SRV(&ctl_setup_pub, &ctl_setup_server),
};

static esp_ble_mesh_model_t hue_models[] = {
    ESP_BLE_MESH_MODEL_GEN_LEVEL_SRV(&level_pub_1, &level_server_1),
    ESP_BLE_MESH_MODEL_LIGHT_HSL_HUE_SRV(&hsl_hue_pub, &hsl_hue_server),
};

static esp_ble_mesh_model_t saturation_models[] = {
    ESP_BLE_MESH_MODEL_GEN_LEVEL_SRV(&level_pub_2, &level_server_2),
    ESP_BLE_MESH_MODEL_LIGHT_HSL_SAT_SRV(&hsl_saturation_pub, &hsl_saturation_server),
};

static esp_ble_mesh_model_t temperature_models[] = {
    ESP_BLE_MESH_MODEL_GEN_LEVEL_SRV(&level_pub_3, &level_server_3),
    ESP_BLE_MESH_MODEL_LIGHT_CTL_TEMP_SRV(&ctl_temperature_pub, &ctl_temperature_server),
};

static esp_ble_mesh_model_op_t fast_prov_srv_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_SET,          3),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_ADD,      16),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR,         2),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_GET,     0),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_GROUP_ADD,    2),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_GROUP_DELETE, 2),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_op_t fast_prov_cli_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_STATUS,    1),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_STATUS, 2),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_ACK,  0),
    ESP_BLE_MESH_MODEL_OP_END,
};

esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, ESP_BLE_MESH_VND_MODEL_ID_FAST_PROV_CLI, fast_prov_cli_op, NULL, &fast_prov_client),
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, ESP_BLE_MESH_VND_MODEL_ID_FAST_PROV_SRV, fast_prov_srv_op, NULL, &fast_prov_server),
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, BLE_MESH_VND_MODEL_ID_OTA_CLIENT, ota_client_op, NULL,            &ota_client),
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, BLE_MESH_VND_MODEL_ID_OTA_SERVER, ota_server_op, &ota_server_pub, &ota_server),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models,        vnd_models),
    // ESP_BLE_MESH_ELEMENT(0, hue_models,         ESP_BLE_MESH_MODEL_NONE),
    // ESP_BLE_MESH_ELEMENT(0, saturation_models,  ESP_BLE_MESH_MODEL_NONE),
    // ESP_BLE_MESH_ELEMENT(0, temperature_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid           = CID_ESP,
    .elements      = elements,
    .element_count = ARRAY_SIZE(elements),
};

/* Disable OOB security for SILabs Android app */
static esp_ble_mesh_prov_t provision = {
    .uuid                = dev_uuid,
    .prov_attention      = 0x00,
    .prov_algorithm      = 0x00,
    .prov_pub_key_oob    = 0x00,
    .prov_static_oob_val = NULL,
    .prov_static_oob_len = 0x00,
    .flags               = 0x00,
    .iv_index            = 0x00,
#if 0
    .output_size    = 4,
    .output_actions = ESP_BLE_MESH_DISPLAY_NUMBER,
    .input_actions  = ESP_BLE_MESH_PUSH,
    .input_size     = 4,
#else
    .output_size    = 0,
    .output_actions = 0,
#endif
};

void user_event(BLE_MESH_APP_EVENT event, void *p_arg);

esp_err_t example_print_local_composition(void)
{
    int                  i, j, k                   = 0;
    esp_ble_mesh_elem_t  *elem                     = NULL;
    esp_ble_mesh_model_t *model                    = NULL;
    const                esp_ble_mesh_comp_t *comp = NULL;

    comp = esp_ble_mesh_get_composition_data();

    ESP_LOGI(TAG, "************************************************");
    ESP_LOGI(TAG, "* cid: 0x%04x    pid: 0x%04x    vid: 0x%04x    *", comp->cid, comp->pid, comp->vid);
    ESP_LOGI(TAG, "* Element Number: 0x%02x                         *", comp->element_count);
    for (i = 0; i < comp->element_count; i++) {
        elem = &comp->elements[i];
        ESP_LOGI(TAG, "* Element %d: 0x%04x                            *", i, elem->element_addr);
        ESP_LOGI(TAG, "*     Loc: 0x%04x   NumS: 0x%02x   NumV: 0x%02x    *", elem->location, elem->sig_model_count, elem->vnd_model_count);
        for (j = 0; j < elem->sig_model_count; j++) {
            model = &elem->sig_models[j];
            ESP_LOGI(TAG, "*     sig_model %02d: id - 0x%04x                *", j, model->model_id);
            for (k = 0; k < ARRAY_SIZE(model->keys); k++) {
                ESP_LOGI(TAG, "*          key 0x%04x                          *", model->keys[k]);
            }
            for (k = 0; k < ARRAY_SIZE(model->groups); k++) {
                ESP_LOGI(TAG, "*          group 0x%04x                        *", model->groups[k]);
            }
            // ESP_LOGI(TAG, "*          publish addr 0x%04x                 *", model->pub->publish_addr);
        }

        for (j = 0; j < elem->vnd_model_count; j++) {
            model = &elem->vnd_models[j];
            ESP_LOGI(TAG, "*     vnd_model %02d: id - 0x%04x, cid - 0x%04x  *", j, model->vnd.model_id, model->vnd.company_id);
            for (k = 0; k < ARRAY_SIZE(model->keys); k++) {
                ESP_LOGI(TAG, "*          key 0x%04x                          *", model->keys[k]);
            }
            for (k = 0; k < ARRAY_SIZE(model->groups); k++) {
                ESP_LOGI(TAG, "*          group 0x%04x                        *", model->groups[k]);
            }
            // ESP_LOGI(TAG, "*          publish addr 0x%04x                 *", model->pub->publish_addr);
        }
    }
    ESP_LOGI(TAG, "************************************************");

    ((void) model);

    return ESP_OK;
}

esp_err_t example_handle_config_app_bind_evt(uint16_t app_idx)
{
    static bool first = true;

    ESP_LOGI(TAG, "%s, app_idx: 0x%04x, primary element address: 0x%04x", __FUNCTION__, app_idx, esp_ble_mesh_get_primary_element_address());
    if (!first) {
        return ESP_OK;
    }
    first = false;

    return ESP_OK;

    example_print_local_composition();

    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_HEALTH_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_DEF_TRANS_TIME_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_POWER_ONOFF_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_POWER_ONOFF_SETUP_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SETUP_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SETUP_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV, app_idx));
    ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SETUP_SRV, app_idx));

    // ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address() + 1, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, app_idx));
    // ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address() + 1, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_HUE_SRV, app_idx));

    // ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address() + 2, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, app_idx));
    // ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address() + 2, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SAT_SRV, app_idx));

    // ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address() + 3, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, app_idx));
    // ESP_ERROR_CHECK(esp_ble_mesh_node_bind_app_key_to_local_model(esp_ble_mesh_get_primary_element_address() + 3, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_TEMP_SRV, app_idx));

    return ESP_OK;
}

esp_err_t example_handle_config_sub_add_evt(uint16_t sub_addr)
{
    ESP_LOGI(TAG, "%s, sub_addr: 0x%04x, primary element address: 0x%04x", __FUNCTION__, sub_addr, esp_ble_mesh_get_primary_element_address());

    // ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_HEALTH_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_DEF_TRANS_TIME_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_POWER_ONOFF_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_POWER_ONOFF_SETUP_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SETUP_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SETUP_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SETUP_SRV, sub_addr));

    // ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 1, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, sub_addr));
    // ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 1, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_HUE_SRV, sub_addr));

    // ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 2, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, sub_addr));
    // ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 2, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SAT_SRV, sub_addr));

    // ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 3, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, sub_addr));
    // ESP_ERROR_CHECK(esp_ble_mesh_model_subscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 3, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_TEMP_SRV, sub_addr));

    return ESP_OK;
}

esp_err_t example_handle_config_sub_delete_evt(uint16_t sub_addr)
{
    ESP_LOGI(TAG, "%s, sub_addr: 0x%04x, primary element address: 0x%04x", __FUNCTION__, sub_addr, esp_ble_mesh_get_primary_element_address());

    // ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_HEALTH_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_DEF_TRANS_TIME_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_POWER_ONOFF_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_POWER_ONOFF_SETUP_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SETUP_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SETUP_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV, sub_addr));
    ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address(), BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SETUP_SRV, sub_addr));

    // ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 1, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, sub_addr));
    // ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 1, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_HUE_SRV, sub_addr));

    // ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 2, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, sub_addr));
    // ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 2, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SAT_SRV, sub_addr));

    // ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 3, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV, sub_addr));
    // ESP_ERROR_CHECK(esp_ble_mesh_model_unsubscribe_group_addr(esp_ble_mesh_get_primary_element_address() + 3, BLE_MESH_CID_NVAL, ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_TEMP_SRV, sub_addr));

    return ESP_OK;
}

/**
 * @brief Current only update first element
 * 
 * @param elem_state 
 * @return esp_err_t 
 */
esp_err_t example_update_server_model_state(elem_state_t *elem_state)
{
    uint16_t                          lightness = 0;
    esp_ble_mesh_server_state_value_t state     = {0};

    lightness = elem_state->state.actual[T_CUR];
    elem_state->state.onoff[T_CUR] = true; /**< Defaule Switch ON */

    // state.gen_onoff.onoff = elem_state->state.onoff[T_CUR];
    // esp_ble_mesh_server_model_update_state(onoff_server.model, ESP_BLE_MESH_GENERIC_ONOFF_STATE, &state);
    // vTaskDelay(pdMS_TO_TICKS(20));

    // state.gen_level.level = elem_state->state.level[T_CUR];
    // esp_ble_mesh_server_model_update_state(level_server_0.model, ESP_BLE_MESH_GENERIC_LEVEL_STATE, &state);
    // vTaskDelay(pdMS_TO_TICKS(20));

    // state.light_hsl_lightness.lightness = lightness;
    // esp_ble_mesh_server_model_update_state(hsl_server.model, ESP_BLE_MESH_LIGHT_HSL_LIGHTNESS_STATE, &state);
    // vTaskDelay(pdMS_TO_TICKS(20));
    // state.light_ctl_lightness.lightness = lightness;
    // esp_ble_mesh_server_model_update_state(ctl_server.model, ESP_BLE_MESH_LIGHT_CTL_LIGHTNESS_STATE, &state);
    // vTaskDelay(pdMS_TO_TICKS(20));
    // state.light_lightness_actual.lightness = lightness;
    // esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_ACTUAL_STATE, &state);
    // vTaskDelay(pdMS_TO_TICKS(20));
    // state.light_lightness_linear.lightness = convert_lightness_actual_to_linear(lightness);
    // esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_LINEAR_STATE, &state);
    // vTaskDelay(pdMS_TO_TICKS(20));

    // state.light_hsl_hue.hue = elem_state->state.hue[T_CUR];
    // esp_ble_mesh_server_model_update_state(hsl_server.model, ESP_BLE_MESH_LIGHT_HSL_HUE_STATE, &state);
    // vTaskDelay(pdMS_TO_TICKS(20));
    // state.light_hsl_saturation.saturation = elem_state->state.saturation[T_CUR];
    // esp_ble_mesh_server_model_update_state(hsl_server.model, ESP_BLE_MESH_LIGHT_HSL_SATURATION_STATE, &state);
    // vTaskDelay(pdMS_TO_TICKS(20));

    // state.light_ctl_temp_delta_uv.delta_uv    = elem_state->state.UV[T_CUR];
    // state.light_ctl_temp_delta_uv.temperature = elem_state->state.temp[T_CUR];
    // esp_ble_mesh_server_model_update_state(ctl_temperature_server.model, ESP_BLE_MESH_LIGHT_CTL_TEMP_DELTA_UV_STATE, &state);
    // vTaskDelay(pdMS_TO_TICKS(20));

    onoff_server.state.onoff   = elem_state->state.onoff[T_CUR];
    level_server_0.state.level = elem_state->state.level[T_CUR];

    ctl_state.lightness   = elem_state->state.lightness[T_CUR];
    ctl_state.delta_uv    = elem_state->state.UV[T_CUR];
    ctl_state.temperature = elem_state->state.temp[T_CUR];

    lightness_state.lightness_last   = lightness;
    lightness_state.lightness_actual = lightness;
    lightness_state.lightness_linear = convert_lightness_actual_to_linear(lightness);

    hsl_state.hue        = elem_state->state.hue[T_CUR];
    hsl_state.saturation = elem_state->state.saturation[T_CUR];
    hsl_state.lightness  = elem_state->state.lightness[T_CUR];

    ESP_LOGI("update", "onoff: %d", elem_state->state.onoff[T_CUR]);
    ESP_LOGI("update", "level: %d", elem_state->state.level[T_CUR]);
    ESP_LOGI("update", "actual: %d", elem_state->state.actual[T_CUR]);
    ESP_LOGI("update", "temp: %d", elem_state->state.temp[T_CUR]);
    ESP_LOGI("update", "uv: %d", elem_state->state.UV[T_CUR]);
    ESP_LOGI("update", "hue: %d", elem_state->state.hue[T_CUR]);
    ESP_LOGI("update", "saturation: %d", elem_state->state.saturation[T_CUR]);
    ESP_LOGI("update", "lightness: %d", elem_state->state.lightness[T_CUR]);
    ESP_LOGI("update", "line lightness: %d", state.light_lightness_linear.lightness);

    return ESP_OK;
}

static void node_prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ENTER_FUNC();
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "net_idx: 0x%04x, addr: 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08x", flags, iv_index);

    /* Updates the net_idx used by Fast Prov Server model, and it can also
     * be updated if the Fast Prov Info Set message contains a valid one.
     */
    fast_prov_server.net_idx = net_idx;

    if (ota_nvs_data.dev_flag & BLE_MESH_OTA_UPDATE_DONE) {
        /* If the device is upgraded, subscribe the group address to ota client */
        err = esp_ble_mesh_model_subscribe_group_addr(ota_nvs_data.own_addr, CID_ESP, BLE_MESH_VND_MODEL_ID_OTA_CLIENT, ota_client_data.group_addr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to subscribe group address: 0x%04x, err: %d", ota_client_data.group_addr, err);
        }
    } else {
        if (!(ota_nvs_data.dev_flag & BLE_MESH_NODE_ADDR_STORED)) {
            /* Add unicast address of primary element */
            ota_nvs_data.own_addr = addr;
            ota_nvs_data.dev_flag |= BLE_MESH_NODE_ADDR_STORED;
            ESP_LOGI(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
            err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store %s, err: %d", OTA_NVS_KEY, err);
                return;
            }
        }

        if (!(ota_nvs_data.dev_flag & BLE_MESH_NODE_APPKEY_ADDED)) {
            ESP_LOGI(TAG, "esp_ble_mesh_node_add_local_app_key");
            /* Add AppKey for ota client & server */
            err = esp_ble_mesh_node_add_local_app_key(app_keys[ota_server.bin_id - 1].key,
                    app_keys[ota_server.bin_id - 1].net_idx,
                    app_keys[ota_server.bin_id - 1].app_idx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to add AppKey to node, err: %d", err);
                return;
            }
        }
    }
}

void button_event(void)
{
    ENTER_FUNC();
    user_event(BLE_MESH_APP_EVT_BOARD_BUTTON_TAP, NULL);
}

void user_event(BLE_MESH_APP_EVENT event, void *p_arg)
{
    BLE_MESH_APP_EVENT next_event = event;
    ESP_LOGI(TAG, "%s, event: %d", __FUNCTION__, event);

    switch (event) {
    case BLE_MESH_APP_EVT_RESET_HW_START:
        // Todo:
        //     1. led operation
        break;
    case BLE_MESH_APP_EVT_RESET_HW_DONE:
        // Todo:
        //     1. led operation
        //     2. esp_restart or other.
        break;
    case BLE_MESH_APP_EVT_MESH_INIT:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_INIT");
        if (*(int *)p_arg == -EALREADY) {
            next_event = BLE_MESH_APP_EVT_MESH_PROV_ALREADY;
        } else if (*(int *)p_arg == 0) {
            next_event = BLE_MESH_APP_EVT_MESH_UNPRO_ADV_START;

            ota_nvs_data.dev_flag = 0x00;
            memset(&ota_nvs_data.dev_flag, 0, sizeof(ble_mesh_ota_nvs_data_t));
            ESP_LOGI(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
            esp_err_t err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store %s, err: %d", OTA_NVS_KEY, err);
            }
        }
        break;
    case BLE_MESH_APP_EVT_MESH_PROV_ALREADY:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_PROV_ALREADY");
        // Load light state
        next_event = BLE_MESH_APP_EVT_STATE_LOAD;
        break;
    case BLE_MESH_APP_EVT_MESH_UNPRO_ADV_START:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_UNPRO_ADV_START");
        // start led blink
        light_driver_breath_start(0, 255, 0); /**< green blink */
        break;
    case BLE_MESH_APP_EVT_MESH_PROV_START:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_PROV_START");
        // stop led blink
        light_driver_breath_stop();
        light_driver_set_rgb(255, 0, 0);
        break;
    case BLE_MESH_APP_EVT_MESH_PROV_TIMEOUT:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_PROV_TIMEOUT");
        // stop led blink
        light_driver_breath_stop();
        break;
    case BLE_MESH_APP_EVT_MESH_PROV_SUCCESS:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_PROV_SUCCESS");
        light_driver_set_rgb(0, 255, 0);
        break;
    case BLE_MESH_APP_EVT_MESH_PROV_FAIL:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_PROV_FAIL");
        // start led blink
        light_driver_breath_start(0, 255, 0); /**< green blink */
        break;
    case BLE_MESH_APP_EVT_MESH_PROV_RESET:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_PROV_RESET");

        // erase all nvs data partition
        // ESP_ERROR_CHECK(nvs_flash_erase());
        // ESP_ERROR_CHECK(nvs_flash_erase_partition("ble_mesh"));

        /**< Reset light state */
        reset_light_state();

        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        break;
    case BLE_MESH_APP_EVT_STATE_LOAD: {
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_STATE_LOAD");

        /**< Load light state */
        load_light_state();

        // update led actual status
        for (size_t i = 0; i < LIGHT_MESH_ELEM_STATE_COUNT; i++) {
            /**< Update Server Model state */
            example_update_server_model_state(&g_elem_state[i]);

            /**< Update LED actual status */
            user_event(BLE_MESH_APP_EVT_STATE_ACTION_DONE, &g_elem_state[i]);
        }
        break;
    }
    case BLE_MESH_APP_EVT_STATE_UPDATE: {
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_STATE_UPDATE");
        elem_state_t *p_elem = (elem_state_t *)p_arg;
#ifdef CONFIG_MESH_MODEL_GEN_ONOFF_SRV
        ESP_LOGD(TAG, "onoff cur(%d) tar(%d)", p_elem->state.onoff[T_CUR], p_elem->state.onoff[T_TAR]);
        if (p_elem->state.onoff[T_CUR] != p_elem->state.onoff[T_TAR]) {
            ESP_LOGI(TAG, "onoff tar(%d)", p_elem->state.onoff[T_TAR]);
            p_elem->state.onoff[T_CUR] = p_elem->state.onoff[T_TAR];
        }
#endif

#ifdef CONFIG_MESH_MODEL_LIGHTNESS_SRV
        ESP_LOGD(TAG, "actual cur(%d) tar(%d)", p_elem->state.actual[T_CUR], p_elem->state.actual[T_TAR]);
        if (p_elem->state.actual[T_CUR] != p_elem->state.actual[T_TAR]) {
            ESP_LOGI(TAG, "actual tar(%d)", p_elem->state.actual[T_TAR]);
            p_elem->state.actual[T_CUR] = p_elem->state.actual[T_TAR];
        }
#endif

#ifdef CONFIG_MESH_MODEL_CTL_SRV
        ESP_LOGD(TAG, "temp cur(%d) tar(%d)", p_elem->state.temp[T_CUR], p_elem->state.temp[T_TAR]);
        if (p_elem->state.temp[T_CUR] != p_elem->state.temp[T_TAR]) {
            ESP_LOGI(TAG, "temp tar(%d)", p_elem->state.temp[T_TAR]);
            p_elem->state.temp[T_CUR] = p_elem->state.temp[T_TAR];
        }

        ESP_LOGD(TAG, "uv cur(%d) tar(%d)", p_elem->state.UV[T_CUR], p_elem->state.UV[T_TAR]);
        if (p_elem->state.UV[T_CUR] != p_elem->state.UV[T_TAR]) {
            ESP_LOGI(TAG, "uv tar(%d)", p_elem->state.UV[T_TAR]);
            p_elem->state.UV[T_CUR] = p_elem->state.UV[T_TAR];
        }
#endif

#ifdef CONFIG_MESH_MODEL_HSL_SRV
        ESP_LOGD(TAG, "hue cur(%d) tar(%d)", p_elem->state.hue[T_CUR], p_elem->state.hue[T_TAR]);
        if (p_elem->state.hue[T_CUR] != p_elem->state.hue[T_TAR]) {
            ESP_LOGI(TAG, "hue tar(%d)", p_elem->state.hue[T_TAR]);
            p_elem->state.hue[T_CUR] = p_elem->state.hue[T_TAR];
        }

        ESP_LOGD(TAG, "saturation cur(%d) tar(%d)", p_elem->state.saturation[T_CUR], p_elem->state.saturation[T_TAR]);
        if (p_elem->state.saturation[T_CUR] != p_elem->state.saturation[T_TAR]) {
            ESP_LOGI(TAG, "saturation tar(%d)", p_elem->state.saturation[T_TAR]);
            p_elem->state.saturation[T_CUR] = p_elem->state.saturation[T_TAR];
        }
#endif

#if defined(CONFIG_MESH_MODEL_CTL_SRV) || defined(CONFIG_MESH_MODEL_HSL_SRV)
        ESP_LOGD(TAG, "lightness cur(%d) tar(%d)", p_elem->state.lightness[T_CUR], p_elem->state.lightness[T_TAR]);
        if (p_elem->state.lightness[T_CUR] != p_elem->state.lightness[T_TAR]) {
            ESP_LOGI(TAG, "lightness tar(%d)", p_elem->state.lightness[T_TAR]);
            p_elem->state.lightness[T_CUR] = p_elem->state.lightness[T_TAR];
        }
#endif
        // update led actual status
        next_event = BLE_MESH_APP_EVT_STATE_ACTION_DONE;
        break;
    }
    case BLE_MESH_APP_EVT_STATE_ACTION_DONE: {
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_STATE_ACTION_DONE");
        elem_state_t *p_elem = (elem_state_t *)p_arg;
#if defined(CONFIG_MESH_MODEL_HSL_SRV) || defined(CONFIG_MESH_MODEL_CTL_SRV)
        if (p_elem->state.color_type == COLOR_HSL) {
#ifdef CONFIG_MESH_MODEL_HSL_SRV
            ESP_LOGI(TAG, "hue: %d, saturation: %d, lightness: %d", p_elem->state.hue[T_CUR], p_elem->state.saturation[T_CUR], p_elem->state.lightness[T_CUR]);
            board_led_hsl(p_elem->elem_index, p_elem->state.hue[T_CUR], p_elem->state.saturation[T_CUR], p_elem->state.lightness[T_CUR]);
#endif
        } else if (p_elem->state.color_type == COLOR_CTL) {
#ifdef CONFIG_MESH_MODEL_CTL_SRV
            ESP_LOGI(TAG, "temp: %d, lightness: %d", p_elem->state.temp[T_CUR], p_elem->state.lightness[T_CUR]);
            board_led_ctl(p_elem->elem_index, p_elem->state.temp[T_CUR], p_elem->state.lightness[T_CUR]);
#endif
        }
#else defined(CONFIG_MESH_MODEL_LIGHTNESS_SRV)
        ESP_LOGI(TAG, "actual: %d", p_elem->state.actual[T_CUR]);
        board_led_lightness(p_elem->elem_index, p_elem->state.actual[T_CUR]);
#endif

#ifdef CONFIG_MESH_MODEL_GEN_ONOFF_SRV
        ESP_LOGI(TAG, "onoff: %d", p_elem->state.onoff[T_CUR]);
        board_led_switch(p_elem->elem_index, p_elem->state.onoff[T_CUR]);
#endif
        if (event == BLE_MESH_APP_EVT_STATE_ACTION_DONE) {
            save_light_state(p_elem);
        }
        break;
    }
    case BLE_MESH_APP_EVT_BOARD_BUTTON_TAP:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_BOARD_BUTTON_TAP");
        // update index 0 led status
        g_elem_state[0].state.onoff[T_TAR] = !g_elem_state[0].state.onoff[T_TAR];
        next_event = BLE_MESH_APP_EVT_STATE_UPDATE;
        p_arg = &g_elem_state[0];
        break;
    case BLE_MESH_APP_EVT_OTA_RECV_START:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_OTA_RECV_START");
        light_driver_breath_start(255, 255, 0); /**< yellow blink */
        break;
    case BLE_MESH_APP_EVT_OTA_SEND_START:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_OTA_SEND_START");
        light_driver_breath_start(0, 0, 255); /**< blue blink */
        break;
    case BLE_MESH_APP_EVT_OTA_RECV_SUCCESS:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_OTA_RECV_SUCCESS");
        light_driver_breath_start(0, 255, 0); /**< green blink */
        break;
    case BLE_MESH_APP_EVT_OTA_SEND_SUCCESS:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_OTA_SEND_SUCCESS");
        light_driver_breath_start(0, 255, 255); /**< cyan-blue blink */
        break;
    case BLE_MESH_APP_EVT_OTA_RECV_FAILED:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_OTA_RECV_FAILED");
        light_driver_breath_start(255, 0, 0); /**< red blink */
        break;
    case BLE_MESH_APP_EVT_OTA_SEND_FAILED:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_OTA_SEND_FAILED");
        light_driver_breath_start(255, 0, 255); /**< purple blink */
        break;
    case BLE_MESH_APP_EVT_MESH_SUB_ADD:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_SUB_ADD");
        break;
    case BLE_MESH_APP_EVT_MESH_SUB_DEL:
        ESP_LOGI(TAG, "BLE_MESH_APP_EVT_MESH_SUB_DEL");
        break;
    default:
        ESP_LOGI(TAG, "unhandle this event: %d", event);
        break;
    }

    if (next_event != event) {
        // send event to user event loop
        user_event(next_event, p_arg);
    }
}

static void provisioner_prov_link_open(esp_ble_mesh_prov_bearer_t bearer)
{
    ENTER_FUNC();
    ESP_LOGI(TAG, "%s: bearer: %s", __FUNCTION__, bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
}

static void provisioner_prov_link_close(esp_ble_mesh_prov_bearer_t bearer, uint8_t reason)
{
    ENTER_FUNC();
    ESP_LOGI(TAG, "%s: bearer: %s, reason: 0x%02x", __FUNCTION__,
             bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", reason);
    if (prov_start_num) {
        prov_start_num--;
    }
}

static void provisioner_prov_complete(int node_idx, const uint8_t uuid[16], uint16_t unicast_addr,
        uint8_t element_num, uint16_t net_idx)
{
    ENTER_FUNC();
    esp_err_t           err   = ESP_OK;
    example_node_info_t *node = NULL;

    if (example_is_node_exist(uuid) == false) {
        fast_prov_server.prov_node_cnt++;
    }

    ESP_LOG_BUFFER_HEX("Device uuid", uuid + 2, 6);
    ESP_LOGI(TAG, "Unicast address: 0x%04x", unicast_addr);

    /* Sets node info */
    err = example_store_node_info(uuid, unicast_addr, element_num, net_idx,
                                  fast_prov_server.app_idx, LED_OFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to set node info", __FUNCTION__);
        return;
    }

    /* Gets node info */
    node = example_get_node_info(unicast_addr);
    if (!node) {
        ESP_LOGE(TAG, "%s: Failed to get node info", __FUNCTION__);
        return;
    }

    if (fast_prov_server.primary_role == true) {
        /* If the Provisioner is the primary one (i.e. provisioned by the phone), it shall
         * store self-provisioned node addresses;
         * If the node_addr_cnt configured by the phone is small than or equal to the
         * maximum number of nodes it can provision, it shall reset the timer which is used
         * to send all node addresses to the phone.
         */
        err = example_store_remote_node_address(unicast_addr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to store node address: 0x%04x", __FUNCTION__, unicast_addr);
            return;
        }
        if (fast_prov_server.node_addr_cnt != FAST_PROV_NODE_COUNT_MIN &&
                fast_prov_server.node_addr_cnt <= fast_prov_server.max_node_num) {
            if (bt_mesh_atomic_test_and_clear_bit(fast_prov_server.srv_flags, GATT_PROXY_ENABLE_START)) {
                k_delayed_work_cancel(&fast_prov_server.gatt_proxy_enable_timer);
            }
            if (!bt_mesh_atomic_test_and_set_bit(fast_prov_server.srv_flags, GATT_PROXY_ENABLE_START)) {
                k_delayed_work_submit(&fast_prov_server.gatt_proxy_enable_timer, GATT_PROXY_ENABLE_TIMEOUT);
                ESP_LOGI(TAG, "start Enable GATT Proxy Timer");
            }
        }
    } else {
        /* When a device is provisioned, the non-primary Provisioner shall reset the timer
         * which is used to send node addresses to the primary Provisioner.
         */
        if (bt_mesh_atomic_test_and_clear_bit(&fast_prov_cli_flags, SEND_SELF_PROV_NODE_ADDR_START)) {
            k_delayed_work_cancel(&send_self_prov_node_addr_timer);
        }
        if (!bt_mesh_atomic_test_and_set_bit(&fast_prov_cli_flags, SEND_SELF_PROV_NODE_ADDR_START)) {
            k_delayed_work_submit(&send_self_prov_node_addr_timer, SEND_SELF_PROV_NODE_ADDR_TIMEOUT);
            ESP_LOGI(TAG, "start Send Self Provision Node Address Timer");
        }
    }

    if (bt_mesh_atomic_test_bit(fast_prov_server.srv_flags, DISABLE_FAST_PROV_START)) {
        /* When a device is provisioned, and the stop_prov flag of the Provisioner has been
         * set, the Provisioner shall reset the timer which is used to stop the provisioner
         * functionality.
         */
        k_delayed_work_cancel(&fast_prov_server.disable_fast_prov_timer);
        k_delayed_work_submit(&fast_prov_server.disable_fast_prov_timer, DISABLE_FAST_PROV_TIMEOUT);
        ESP_LOGI(TAG, "start Disable Fast Provision Timer");
    }

    /* The Provisioner will send Config AppKey Add to the node. */
    example_msg_common_info_t info = {
        .net_idx = node->net_idx,
        .app_idx = node->app_idx,
        .dst     = node->unicast_addr,
        .role    = ROLE_FAST_PROV,
        .timeout = 0,
    };
    err = example_send_config_appkey_add(config_client.model, &info, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to send Config AppKey Add message", __FUNCTION__);
        return;
    }
}

static void example_recv_unprov_adv_pkt(uint8_t dev_uuid[16], uint8_t addr[BLE_MESH_ADDR_LEN],
        esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info, uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer)
{
    ENTER_FUNC();
    esp_err_t                     err     = ESP_OK;
    esp_ble_mesh_unprov_dev_add_t add_dev = {0};
    esp_ble_mesh_dev_add_flag_t   flag    = {0};

    /* In Fast Provisioning, the Provisioner should only use PB-ADV to provision devices. */
    if (prov_start && (bearer & ESP_BLE_MESH_PROV_ADV)) {
        /* Checks if the device is a reprovisioned one. */
        if (example_is_node_exist(dev_uuid) == false) {
            if ((prov_start_num >= fast_prov_server.max_node_num) ||
                    (fast_prov_server.prov_node_cnt >= fast_prov_server.max_node_num)) {
                return;
            }
        }

        add_dev.addr_type = (uint8_t)addr_type;
        add_dev.oob_info  = oob_info;
        add_dev.bearer    = (uint8_t)bearer;
        memcpy(add_dev.uuid, dev_uuid, 16);
        memcpy(add_dev.addr, addr, BLE_MESH_ADDR_LEN);
        flag = ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG;
        err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev, flag);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to start provisioning device", __FUNCTION__);
            return;
        }

        /* If adding unprovisioned device successfully, increase prov_start_num */
        prov_start_num++;
    }

    return;
}

static void example_change_led_state(esp_ble_mesh_model_t *model,
        esp_ble_mesh_msg_ctx_t *ctx, uint8_t onoff)
{
    ENTER_FUNC();

    // set index 0 led target status
    g_elem_state[0].state.onoff[T_TAR] = onoff;

    /* When the node receives the first Generic OnOff Get/Set/Set Unack message, it will
     * start the timer used to disable fast provisioning functionality.
     */
    if (!bt_mesh_atomic_test_and_set_bit(fast_prov_server.srv_flags, DISABLE_FAST_PROV_START)) {
        k_delayed_work_submit(&fast_prov_server.disable_fast_prov_timer, DISABLE_FAST_PROV_TIMEOUT);
        ESP_LOGI(TAG, "start Disable Fast Provision Timer");
    }
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
        esp_ble_mesh_prov_cb_param_t *param)
{
    ENTER_FUNC();
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "%s, event: 0x%02x", __FUNCTION__, event);

    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code: %d", param->prov_register_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code: %d", param->node_prov_enable_comp.err_code);

        // send event to user event loop
        user_event(BLE_MESH_APP_EVT_MESH_INIT, &param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer: %s",
                 param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");

        // send event to user event loop
        user_event(BLE_MESH_APP_EVT_MESH_PROV_START, NULL);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer: %s",
                 param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");

        if (esp_ble_mesh_node_is_provisioned()) {
            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_PROV_SUCCESS, NULL);
        } else {
            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_PROV_FAIL, NULL);
        }
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT, addr: 0x%04x, net_idx: 0x%04x, iv_index: 0x%08x, flags: 0x%02x",
                 param->node_prov_complete.addr, param->node_prov_complete.net_idx, param->node_prov_complete.iv_index, param->node_prov_complete.flags);

        node_prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
                           param->node_prov_complete.flags, param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROXY_GATT_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROXY_GATT_ENABLE_COMP_EVT, err_code: %d", param->node_proxy_gatt_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROXY_GATT_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROXY_GATT_DISABLE_COMP_EVT, err_code: %d", param->node_proxy_gatt_disable_comp.err_code);
        if (fast_prov_server.primary_role == true) {
            config_server.relay = ESP_BLE_MESH_RELAY_DISABLED;
        }
        prov_start = true;
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT");
        example_recv_unprov_adv_pkt(param->provisioner_recv_unprov_adv_pkt.dev_uuid, param->provisioner_recv_unprov_adv_pkt.addr,
                                    param->provisioner_recv_unprov_adv_pkt.addr_type, param->provisioner_recv_unprov_adv_pkt.oob_info,
                                    param->provisioner_recv_unprov_adv_pkt.adv_type, param->provisioner_recv_unprov_adv_pkt.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT, bearer: %s",
                 param->provisioner_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        provisioner_prov_link_open(param->provisioner_prov_link_open.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT, bearer: %s, reason: %d",
                 param->provisioner_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT",
                 param->provisioner_prov_link_close.reason);

        provisioner_prov_link_close(param->provisioner_prov_link_close.bearer,
                                    param->provisioner_prov_link_close.reason);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT");
        provisioner_prov_complete(param->provisioner_prov_complete.node_idx,
                                  param->provisioner_prov_complete.device_uuid, param->provisioner_prov_complete.unicast_addr,
                                  param->provisioner_prov_complete.element_num, param->provisioner_prov_complete.netkey_idx);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT, err_code: %d",
                 param->provisioner_add_unprov_dev_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT, err_code: %d",
                 param->provisioner_set_dev_uuid_match_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT, err_code: %d, node_index: %d",
                 param->provisioner_set_node_name_comp.err_code, param->provisioner_set_node_name_comp.node_index);
        break;
    case ESP_BLE_MESH_SET_FAST_PROV_INFO_COMP_EVT: {
        ESP_LOGI(TAG, "ESP_BLE_MESH_SET_FAST_PROV_INFO_COMP_EVT, status_unicast: 0x%02x, status_net_idx: 0x%02x, status_match: 0x%02x",
                 param->set_fast_prov_info_comp.status_unicast, param->set_fast_prov_info_comp.status_net_idx, param->set_fast_prov_info_comp.status_match);

        err = example_handle_fast_prov_info_set_comp_evt(fast_prov_server.model, param->set_fast_prov_info_comp.status_unicast,
                param->set_fast_prov_info_comp.status_net_idx, param->set_fast_prov_info_comp.status_match);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to handle Fast Prov Info Set complete event", __FUNCTION__);
            return;
        }
        break;
    }
    case ESP_BLE_MESH_SET_FAST_PROV_ACTION_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_SET_FAST_PROV_ACTION_COMP_EVT, status_action: 0x%02x",
                 param->set_fast_prov_action_comp.status_action);

        err = example_handle_fast_prov_action_set_comp_evt(fast_prov_server.model,
                param->set_fast_prov_action_comp.status_action);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to handle Fast Prov Action Set complete event", __FUNCTION__);
            return;
        }
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_RESET_EVT");

        ota_nvs_data.dev_flag = 0x00;
        memset(&ota_nvs_data.dev_flag, 0, sizeof(ble_mesh_ota_nvs_data_t));
        ESP_LOGI(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
        err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store %s, err: %d", OTA_NVS_KEY, err);
        }

        // send event to user event loop
        user_event(BLE_MESH_APP_EVT_MESH_PROV_RESET, NULL);
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code: %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    case ESP_BLE_MESH_MODEL_SUBSCRIBE_GROUP_ADDR_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_SUBSCRIBE_GROUP_ADDR_COMP_EVT, sub_addr: 0x%04x, err_code: %d",
                 param->model_sub_group_addr_comp.group_addr, param->model_sub_group_addr_comp.err_code);
        break;
    case ESP_BLE_MESH_MODEL_UNSUBSCRIBE_GROUP_ADDR_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_UNSUBSCRIBE_GROUP_ADDR_COMP_EVT, sub_addr: 0x%04x, err_code: %d",
                 param->model_sub_group_addr_comp.group_addr, param->model_unsub_group_addr_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_ADD_LOCAL_APP_KEY_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_ADD_LOCAL_APP_KEY_COMP_EVT, err_code: %d, net_idx: 0x%04x, app_idx: 0x%04x",
                 param->node_add_app_key_comp.err_code, param->node_add_app_key_comp.net_idx, param->node_add_app_key_comp.app_idx);

        if (param->node_add_app_key_comp.err_code == 0 && param->node_add_app_key_comp.app_idx == app_keys[ota_server.bin_id - 1].app_idx) {
            err = esp_ble_mesh_node_bind_app_key_to_local_model(ota_nvs_data.own_addr, CID_ESP, BLE_MESH_VND_MODEL_ID_OTA_SERVER,
                    param->node_add_app_key_comp.app_idx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to bind AppKey with ota server");
            }
        }
        break;
    case ESP_BLE_MESH_NODE_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_BIND_APP_KEY_TO_MODEL_COMP_EVT, err_code: %d, element_addr: 0x%04x, model_id 0x%04x, company_id 0x%04x, app_idx: 0x%04x",
                 param->node_bind_app_key_to_model_comp.err_code, param->node_bind_app_key_to_model_comp.element_addr, param->node_bind_app_key_to_model_comp.model_id,
                 param->node_bind_app_key_to_model_comp.company_id, param->node_bind_app_key_to_model_comp.app_idx);

        if (param->node_bind_app_key_to_model_comp.err_code == 0) {
            if (param->node_bind_app_key_to_model_comp.company_id == CID_ESP) {
                if (param->node_bind_app_key_to_model_comp.model_id == BLE_MESH_VND_MODEL_ID_OTA_SERVER) {
                    err = esp_ble_mesh_node_bind_app_key_to_local_model(ota_nvs_data.own_addr, CID_ESP, BLE_MESH_VND_MODEL_ID_OTA_CLIENT,
                            param->node_bind_app_key_to_model_comp.app_idx);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to bind AppKey with ota client");
                    }
                } else if (param->node_bind_app_key_to_model_comp.model_id == BLE_MESH_VND_MODEL_ID_OTA_CLIENT) {
                    ota_nvs_data.dev_flag |= BLE_MESH_NODE_APPKEY_ADDED;
                    ESP_LOGI(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
                    err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to store %s, err: %d", OTA_NVS_KEY, err);
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

static void example_ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
        esp_ble_mesh_cfg_client_cb_param_t *param)
{
    ENTER_FUNC();
    example_node_info_t *node   = NULL;
    uint32_t            opcode  = 0;
    uint16_t            address = 0;
    esp_err_t           err     = ESP_OK;

    ESP_LOGI(TAG, "%s, error_code: 0x%02x, event: 0x%02x, addr: 0x%04x",
             __FUNCTION__, param->error_code, event, param->params->ctx.addr);

    opcode  = param->params->opcode;
    address = param->params->ctx.addr;

    node = example_get_node_info(address);
    if (!node) {
        ESP_LOGE(TAG, "%s: Failed to get node info", __FUNCTION__);
        return;
    }

    if (param->error_code) {
        ESP_LOGE(TAG, "Failed to send config client message, opcode: 0x%04x", opcode);
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT");
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT");
        switch (opcode) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD: {
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            example_fast_prov_info_set_t set = {0};
            if (node->reprov == false) {
                /* After sending Config AppKey Add successfully, start to send Fast Prov Info Set */
                if (fast_prov_server.unicast_cur >= fast_prov_server.unicast_max) {
                    /* TODO:
                     * 1. If unicast_cur is >= unicast_max, we can also send the message to enable
                     * the Provisioner functionality on the node, and need to add another vendor
                     * message used by the node to require a new unicast address range from primary
                     * Provisioner, and before get the correct response, the node should pend
                     * the fast provisioning functionality.
                     * 2. Currently if address is not enough, the Provisioner will only add the group
                     * address to the node.
                     */
                    ESP_LOGW(TAG, "%s: Not enough address to be assigned", __FUNCTION__);
                    node->lack_of_addr = true;
                } else {
                    /* Send fast_prov_info_set message to node */
                    node->lack_of_addr = false;
                    node->unicast_min  = fast_prov_server.unicast_cur;
                    if (fast_prov_server.unicast_cur + fast_prov_server.unicast_step >= fast_prov_server.unicast_max) {
                        node->unicast_max = fast_prov_server.unicast_max;
                    } else {
                        node->unicast_max = fast_prov_server.unicast_cur + fast_prov_server.unicast_step;
                    }
                    node->flags      = fast_prov_server.flags;
                    node->iv_index   = fast_prov_server.iv_index;
                    node->fp_net_idx = fast_prov_server.net_idx;
                    node->group_addr = fast_prov_server.group_addr;
                    node->prov_addr  = fast_prov_server.prim_prov_addr;
                    node->match_len  = fast_prov_server.match_len;
                    node->action     = FAST_PROV_ACT_ENTER;
                    fast_prov_server.unicast_cur = node->unicast_max + 1;
                    memcpy(node->match_val, fast_prov_server.match_val, fast_prov_server.match_len);
                }
            }
            if (node->lack_of_addr == false) {
                set.ctx_flags = 0x03FE;
                memcpy(&set.unicast_min, &node->unicast_min,
                       sizeof(example_node_info_t) - offsetof(example_node_info_t, unicast_min));
            } else {
                set.ctx_flags  = BIT(6);
                set.group_addr = fast_prov_server.group_addr;
            }
            example_msg_common_info_t info = {
                .net_idx = node->net_idx,
                .app_idx = node->app_idx,
                .dst     = node->unicast_addr,
                .role    = ROLE_FAST_PROV,
                .timeout = 0,
            };
            err = example_send_fast_prov_info_set(fast_prov_client.model, &info, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to send Fast Prov Info Set message", __FUNCTION__);
                return;
            }
            break;
        }
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT");
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT");
        switch (opcode) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD: {
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            example_msg_common_info_t info = {
                .net_idx = node->net_idx,
                .app_idx = node->app_idx,
                .dst     = node->unicast_addr,
                .role    = ROLE_FAST_PROV,
                .timeout = 0,
            };
            err = example_send_config_appkey_add(config_client.model, &info, NULL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to send Config AppKey Add message", __FUNCTION__);
                return;
            }
            break;
        }
        default:
            break;
        }
        break;
    default:
        return;
    }
}

static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
        esp_ble_mesh_cfg_server_cb_param_t *param)
{
    ENTER_FUNC();
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "%s, event = 0x%02x, opcode = 0x%04x, addr: 0x%04x, dst: 0x%04x",
             __FUNCTION__, event, param->ctx.recv_op, param->ctx.addr, param->ctx.recv_dst);

    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD, net_idx: 0x%04x, app_idx: 0x%04x",
                     param->value.state_change.appkey_add.net_idx, param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);

            err = example_handle_config_app_key_add_evt(param->value.state_change.appkey_add.app_idx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to bind app_idx: 0x%04x with non-config models",
                         __FUNCTION__, param->value.state_change.appkey_add.app_idx);
                return;
            }

            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_APPKEY_ADD, NULL);
            break;
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_DELETE:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_DELETE, net_idx: 0x%04x, app_idx: 0x%04x",
                     param->value.state_change.appkey_delete.net_idx, param->value.state_change.appkey_delete.net_idx);

            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_APPKEY_DEL, NULL);
            break;
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_UPDATE:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_UPDATE, net_idx: 0x%04x, app_idx: 0x%04x",
                     param->value.state_change.appkey_add.net_idx, param->value.state_change.appkey_add.net_idx);

            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_APPKEY_UPDATE, NULL);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND, elem_addr: 0x%04x, app_idx: 0x%04x, cid: 0x%04x, mod_id: 0x%04x",
                     param->value.state_change.mod_app_bind.element_addr, param->value.state_change.mod_app_bind.app_idx,
                     param->value.state_change.mod_app_bind.company_id, param->value.state_change.mod_app_bind.model_id);

            err = example_handle_config_app_bind_evt(param->value.state_change.mod_app_bind.app_idx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to add subscription address: 0x%04x with non-config models",
                         __FUNCTION__, param->value.state_change.mod_sub_add.sub_addr);
                return;
            }
            break;
        case ESP_BLE_MESH_MODEL_OP_NET_KEY_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_NET_KEY_ADD, net_idx: 0x%04x", param->value.state_change.netkey_add.net_idx);

            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_NETKEY_ADD, NULL);
            break;
        case ESP_BLE_MESH_MODEL_OP_NET_KEY_DELETE:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_NET_KEY_DELETE, net_idx: 0x%04x", param->value.state_change.netkey_delete.net_idx);

            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_NETKEY_DEL, NULL);
            break;
        case ESP_BLE_MESH_MODEL_OP_NET_KEY_UPDATE:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_NET_KEY_UPDATE, net_idx: 0x%04x", param->value.state_change.netkey_update.net_idx);

            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_NETKEY_UPDATE, NULL);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD, elem_addr: 0x%04x, sub_addr: 0x%04x, cid: 0x%04x, mod_id: 0x%04x",
                     param->value.state_change.mod_sub_add.element_addr, param->value.state_change.mod_sub_add.sub_addr,
                     param->value.state_change.mod_sub_add.company_id, param->value.state_change.mod_sub_add.model_id);

            err = example_handle_config_sub_add_evt(param->value.state_change.mod_sub_add.sub_addr);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to add subscription address: 0x%04x with non-config models",
                         __FUNCTION__, param->value.state_change.mod_sub_add.sub_addr);
                return;
            }

            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_SUB_ADD, NULL);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE, elem_addr: 0x%04x, sub_addr: 0x%04x, cid: 0x%04x, mod_id: 0x%04x",
                     param->value.state_change.mod_sub_delete.element_addr, param->value.state_change.mod_sub_delete.sub_addr,
                     param->value.state_change.mod_sub_delete.company_id, param->value.state_change.mod_sub_delete.model_id);

            err = example_handle_config_sub_delete_evt(param->value.state_change.mod_sub_delete.sub_addr);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to delete subscription address: 0x%04x with non-config models",
                         __FUNCTION__, param->value.state_change.mod_sub_delete.sub_addr);
                return;
            }

            // send event to user event loop
            user_event(BLE_MESH_APP_EVT_MESH_SUB_DEL, NULL);
            break;
        default:
            break;
        }
    }
}

static void example_ble_mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
        esp_ble_mesh_generic_server_cb_param_t *param)
{
    ENTER_FUNC();
    int16_t  level        = 0;
    uint16_t lightness    = 0;
    uint16_t primary_addr = 0;
    esp_ble_mesh_server_state_value_t state = {0};

    ESP_LOGI(TAG, "%s: event: 0x%02x, opcode: 0x%04x, src: 0x%04x, dst: 0x%04x",
             __FUNCTION__, event, param->ctx.recv_op, param->ctx.addr, param->ctx.recv_dst);

    primary_addr = esp_ble_mesh_get_primary_element_address();

    switch (event) {
    case ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT:
        switch (param->ctx.recv_op) {
        /*!< Generic OnOff Message Opcode */
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK:
            if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET, onoff: %d", param->value.state_change.onoff_set.onoff);
            } else if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK, onoff: %d", param->value.state_change.onoff_set.onoff);
            }

            /* Generic OnOff Server Model - Primary Element */
            example_change_led_state(param->model, &param->ctx, param->value.state_change.onoff_set.onoff);

            /* Update bound states */
            if (param->value.state_change.onoff_set.onoff == LED_ON) {
                if (lightness_state.lightness_default == 0x0000) {
                    lightness = lightness_state.lightness_last;
                } else {
                    lightness = lightness_state.lightness_default;
                }
                state.light_lightness_actual.lightness = lightness;
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_ACTUAL_STATE, &state);
                state.light_lightness_linear.lightness = convert_lightness_actual_to_linear(lightness);
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_LINEAR_STATE, &state);
                state.gen_level.level = lightness - 32768;
                esp_ble_mesh_server_model_update_state(level_server_0.model, ESP_BLE_MESH_GENERIC_LEVEL_STATE, &state);
                state.light_hsl_lightness.lightness = lightness;
                esp_ble_mesh_server_model_update_state(hsl_server.model, ESP_BLE_MESH_LIGHT_HSL_LIGHTNESS_STATE, &state);
                state.light_ctl_lightness.lightness = lightness;
                esp_ble_mesh_server_model_update_state(ctl_server.model, ESP_BLE_MESH_LIGHT_CTL_LIGHTNESS_STATE, &state);

                // set index 0 led target status
                g_elem_state[0].state.lightness[T_TAR] = lightness;
                g_elem_state[0].state.actual[T_TAR]    = lightness;
                g_elem_state[0].state.linear[T_TAR]    = convert_lightness_actual_to_linear(lightness);
            }
            // set index 0 led target status
            g_elem_state[0].state.onoff[T_TAR]  = param->value.state_change.onoff_set.onoff;

            // update index 0 led status
            user_event(BLE_MESH_APP_EVT_STATE_UPDATE, &g_elem_state[0]);
            break;
        /*!< Generic Level Message Opcode */
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET:
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET_UNACK:
            /* Generic Level Server Model - Primary/Secondary Element(s) */
            if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET) {
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET, level: %d", param->value.state_change.level_set.level);
            } else if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET_UNACK) {
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET_UNACK, level: %d", param->value.state_change.level_set.level);
            }

            /* Update bound states */
            level = param->value.state_change.level_set.level;
            if (param->model->element->element_addr == primary_addr) {
                /* Change corresponding bound states in root element */
                state.light_lightness_actual.lightness = level + 32768;
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_ACTUAL_STATE, &state);
                state.light_lightness_linear.lightness = convert_lightness_actual_to_linear(level + 32768);
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_LINEAR_STATE, &state);
                state.gen_onoff.onoff = (level + 32768) ? LED_ON : LED_OFF;
                esp_ble_mesh_server_model_update_state(onoff_server.model, ESP_BLE_MESH_GENERIC_ONOFF_STATE, &state);
                state.light_hsl_lightness.lightness = level + 32768;
                esp_ble_mesh_server_model_update_state(hsl_server.model, ESP_BLE_MESH_LIGHT_HSL_LIGHTNESS_STATE, &state);
                state.light_ctl_lightness.lightness = level + 32768;
                esp_ble_mesh_server_model_update_state(ctl_server.model, ESP_BLE_MESH_LIGHT_CTL_LIGHTNESS_STATE, &state);

                // set index 0 led target status
                g_elem_state[0].state.onoff[T_TAR]     = (level + 32768) ? LED_ON : LED_OFF;
                g_elem_state[0].state.lightness[T_TAR] = level + 32768;
                g_elem_state[0].state.actual[T_TAR]    = level + 32768;
                g_elem_state[0].state.linear[T_TAR]    = convert_lightness_actual_to_linear(level + 32768);

                // update index 0 led status
                user_event(BLE_MESH_APP_EVT_STATE_UPDATE, &g_elem_state[0]);
            } else if (param->model->element->element_addr == primary_addr + 1) {
                /* Change corresponding bound states in hue element */
                state.light_hsl_hue.hue = level + 32768;
                esp_ble_mesh_server_model_update_state(hsl_hue_server.model, ESP_BLE_MESH_LIGHT_HSL_HUE_STATE, &state);
            } else if (param->model->element->element_addr == primary_addr + 2) {
                /* Change corresponding bound states in saturation element */
                state.light_hsl_saturation.saturation = level + 32768;
                esp_ble_mesh_server_model_update_state(hsl_saturation_server.model, ESP_BLE_MESH_LIGHT_HSL_SATURATION_STATE, &state);
            } else if (param->model->element->element_addr == primary_addr + 3) {
                /* Change corresponding bound states in temperature element */
                state.light_ctl_temp_delta_uv.temperature = covert_level_to_temperature(level,
                        ctl_temperature_server.state->temperature_range_min, ctl_temperature_server.state->temperature_range_max);
                state.light_ctl_temp_delta_uv.delta_uv = ctl_temperature_server.state->delta_uv;
                esp_ble_mesh_server_model_update_state(ctl_temperature_server.model, ESP_BLE_MESH_LIGHT_HSL_SATURATION_STATE, &state);
            }
            break;
        /*!< Generic Power OnOff Setup Message Opcode */
        case ESP_BLE_MESH_MODEL_OP_GEN_ONPOWERUP_SET:
        case ESP_BLE_MESH_MODEL_OP_GEN_ONPOWERUP_SET_UNACK:
            /* Generic Power OnOff Setup Server Model - Primary Element */
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONPOWERUP_SET, onpowerup: %d", param->value.state_change.onpowerup_set.onpowerup);
            break;
        }
        break;
    default:
        ESP_LOGE(TAG, "Unknown Generic Server event: 0x%02x", event);
        break;
    }
}

static void example_ble_mesh_lighting_server_cb(esp_ble_mesh_lighting_server_cb_event_t event,
        esp_ble_mesh_lighting_server_cb_param_t *param)
{
    ENTER_FUNC();
    uint16_t                          lightness = 0;
    esp_ble_mesh_server_state_value_t state     = {0};

    ESP_LOGI(TAG, "%s: event: 0x%02x, opcode: 0x%04x, src: 0x%04x, dst: 0x%04x",
             __FUNCTION__, event, param->ctx.recv_op, param->ctx.addr, param->ctx.recv_dst);

    switch (event) {
    case ESP_BLE_MESH_LIGHTING_SERVER_STATE_CHANGE_EVT:
        switch (param->ctx.recv_op) {
            /*!< Light Lightness Message Opcode */
            {
            case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET_UNACK:
                /* Light Lightness Server Model - Primary Element */
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET Or UNACK, lightness actual: %d",
                         param->value.state_change.lightness_set.lightness);
                /* Update bound states */
                lightness = param->value.state_change.lightness_set.lightness;
                state.light_lightness_linear.lightness = convert_lightness_actual_to_linear(lightness);
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_LINEAR_STATE, &state);
                state.gen_level.level = lightness - 32768;
                esp_ble_mesh_server_model_update_state(level_server_0.model, ESP_BLE_MESH_GENERIC_LEVEL_STATE, &state);
                state.gen_onoff.onoff = lightness ? LED_ON : LED_OFF;
                esp_ble_mesh_server_model_update_state(onoff_server.model, ESP_BLE_MESH_GENERIC_ONOFF_STATE, &state);
                state.light_hsl_lightness.lightness = lightness;
                esp_ble_mesh_server_model_update_state(hsl_server.model, ESP_BLE_MESH_LIGHT_HSL_LIGHTNESS_STATE, &state);
                state.light_ctl_lightness.lightness = lightness;
                esp_ble_mesh_server_model_update_state(ctl_server.model, ESP_BLE_MESH_LIGHT_CTL_LIGHTNESS_STATE, &state);

                // set index 0 led target status
                g_elem_state[0].state.onoff[T_TAR]     = lightness ? LED_ON : LED_OFF;
                g_elem_state[0].state.lightness[T_TAR] = lightness;
                g_elem_state[0].state.actual[T_TAR]    = lightness;
                g_elem_state[0].state.linear[T_TAR]    = state.light_lightness_linear.lightness;

                // update index 0 led status
                user_event(BLE_MESH_APP_EVT_STATE_UPDATE, &g_elem_state[0]);
                break;
            case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_LINEAR_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_LINEAR_SET_UNACK:
                /* Light Lightness Server Model - Primary Element */
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_LINEAR_SET Or UNACK, lightness linear: %d",
                         param->value.state_change.lightness_linear_set.lightness);

                /* Update bound states */
                lightness = convert_lightness_linear_to_actual(param->value.state_change.lightness_linear_set.lightness);
                state.light_lightness_actual.lightness = lightness;
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_ACTUAL_STATE, &state);
                state.gen_level.level = lightness - 32768;
                esp_ble_mesh_server_model_update_state(level_server_0.model, ESP_BLE_MESH_GENERIC_LEVEL_STATE, &state);
                state.gen_onoff.onoff = lightness ? LED_ON : LED_OFF;
                esp_ble_mesh_server_model_update_state(onoff_server.model, ESP_BLE_MESH_GENERIC_ONOFF_STATE, &state);
                state.light_hsl_lightness.lightness = lightness;
                esp_ble_mesh_server_model_update_state(hsl_server.model, ESP_BLE_MESH_LIGHT_HSL_LIGHTNESS_STATE, &state);
                state.light_ctl_lightness.lightness = lightness;
                esp_ble_mesh_server_model_update_state(ctl_server.model, ESP_BLE_MESH_LIGHT_CTL_LIGHTNESS_STATE, &state);

                // set index 0 led target status
                g_elem_state[0].state.onoff[T_TAR]     = lightness ? LED_ON : LED_OFF;
                g_elem_state[0].state.lightness[T_TAR] = lightness;
                g_elem_state[0].state.actual[T_TAR]    = lightness;
                g_elem_state[0].state.linear[T_TAR]    = param->value.state_change.lightness_linear_set.lightness;

                // update index 0 led status
                user_event(BLE_MESH_APP_EVT_STATE_UPDATE, &g_elem_state[0]);
                break;
            /*!< Light Lightness Setup Message Opcode */
            case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_DEFAULT_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_DEFAULT_SET_UNACK:
                /* Light Lightness Setup Server Model - Primary Element */
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_DEFAULT_SET Or UNACK, lightness default: %d",
                         param->value.state_change.lightness_default_set.lightness);

                // set index 0 led status
                g_elem_state[0].powerup.default_actual = param->value.state_change.lightness_default_set.lightness;
                break;
            case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_SET_UNACK:
                /* Light Lightness Setup Server Model - Primary Element */
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_SET, lightness min: %d, max: %d",
                         param->value.state_change.lightness_range_set.range_min, param->value.state_change.lightness_range_set.range_max);

                // set index 0 led status
                g_elem_state[0].powerup.min_actual = param->value.state_change.lightness_range_set.range_min;
                g_elem_state[0].powerup.max_actual = param->value.state_change.lightness_range_set.range_max;
                break;
            }
            /*!< Light CTL Message Opcode */
            {
            case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET_UNACK:
                /* Light CTL Server Model - Primary Element */
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET Or UNACK, lightness: %d, temperature: %d, delta uv: %d",
                         param->value.state_change.ctl_set.lightness, param->value.state_change.ctl_set.temperature, param->value.state_change.ctl_set.delta_uv);

                /* Update bound states */
                lightness = param->value.state_change.ctl_set.lightness;
                state.light_lightness_actual.lightness = lightness;
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_ACTUAL_STATE, &state);
                state.light_lightness_linear.lightness = convert_lightness_actual_to_linear(lightness);
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_LINEAR_STATE, &state);
                state.gen_level.level = lightness - 32768;
                esp_ble_mesh_server_model_update_state(level_server_0.model, ESP_BLE_MESH_GENERIC_LEVEL_STATE, &state);
                state.gen_onoff.onoff = lightness ? LED_ON : LED_OFF;
                esp_ble_mesh_server_model_update_state(onoff_server.model, ESP_BLE_MESH_GENERIC_ONOFF_STATE, &state);
                state.light_hsl_lightness.lightness = lightness;
                esp_ble_mesh_server_model_update_state(hsl_server.model, ESP_BLE_MESH_LIGHT_HSL_LIGHTNESS_STATE, &state);

                // set index 0 led target status
                g_elem_state[0].state.color_type       = COLOR_CTL;
                g_elem_state[0].state.onoff[T_TAR]     = lightness ? LED_ON : LED_OFF;
                g_elem_state[0].state.lightness[T_TAR] = lightness;
                g_elem_state[0].state.actual[T_TAR]    = lightness;
                g_elem_state[0].state.linear[T_TAR]    = state.light_lightness_linear.lightness;
                g_elem_state[0].state.temp[T_TAR]      = param->value.state_change.ctl_set.temperature;
                g_elem_state[0].state.UV[T_TAR]        = param->value.state_change.ctl_set.delta_uv;

                // update index 0 led status
                user_event(BLE_MESH_APP_EVT_STATE_UPDATE, &g_elem_state[0]);
                break;
            case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET_UNACK:
                /* Light CTL Temperature Server Model - Secondary Element */
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET Or UNACK, temperature: %d, delta uv: %d",
                         param->value.state_change.ctl_temp_set.temperature, param->value.state_change.ctl_temp_set.delta_uv);

                state.gen_level.level = convert_temperature_to_level(param->value.state_change.ctl_temp_set.temperature,
                                        ctl_state.temperature_range_min, ctl_state.temperature_range_max);
                esp_ble_mesh_server_model_update_state(level_server_3.model, ESP_BLE_MESH_GENERIC_LEVEL_STATE, &state);

                // set index 0 led target status
                g_elem_state[0].state.color_type  = COLOR_CTL;
                g_elem_state[0].state.temp[T_TAR] = param->value.state_change.ctl_set.lightness;
                g_elem_state[0].state.UV[T_TAR]   = param->value.state_change.ctl_set.temperature;

                // update index 0 led status
                user_event(BLE_MESH_APP_EVT_STATE_UPDATE, &g_elem_state[0]);
                break;
            /*!< Light CTL Setup Message Opcode */
            case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_DEFAULT_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_DEFAULT_SET_UNACK:
                /* Light CTL Setup Server Model - Primary Element */
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_DEFAULT_SET Or UNACK, lightness: %d, temperature: %d, delta uv: %d",
                         param->value.state_change.ctl_default_set.lightness, param->value.state_change.ctl_default_set.temperature, param->value.state_change.ctl_default_set.delta_uv);

                // set index 0 led status
                g_elem_state[0].powerup.default_actual = param->value.state_change.ctl_default_set.lightness;
                g_elem_state[0].powerup.default_temp   = param->value.state_change.ctl_default_set.temperature;
                g_elem_state[0].powerup.default_UV     = param->value.state_change.ctl_default_set.delta_uv;
                break;
            case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_SET_UNACK:
                /* Light CTL Setup Server Model - Primary Element */
                ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_SET Or UNACK, temperature min: %d, max: %d",
                         param->value.state_change.ctl_temp_range_set.range_min, param->value.state_change.ctl_temp_range_set.range_max);

                // set index 0 led status
                g_elem_state[0].powerup.min_temp = param->value.state_change.ctl_temp_range_set.range_min;
                g_elem_state[0].powerup.max_temp = param->value.state_change.ctl_temp_range_set.range_max;
                break;
            }
            /*!< Light HSL Message Opcode */
            {
            case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET_UNACK:
                /* Light HSL Server Model - Primary Element */
                ESP_LOGW(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET Or UNACK, hue: %d, saturation: %d, lightness: %d",
                         param->value.state_change.hsl_set.hue, param->value.state_change.hsl_set.saturation, param->value.state_change.hsl_set.lightness);

                /* Update bound states */
                lightness = param->value.state_change.hsl_set.lightness;
                state.light_lightness_actual.lightness = lightness;
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_ACTUAL_STATE, &state);
                state.light_lightness_linear.lightness = convert_lightness_actual_to_linear(lightness);
                esp_ble_mesh_server_model_update_state(lightness_server.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_LINEAR_STATE, &state);
                state.gen_level.level = lightness - 32768;
                esp_ble_mesh_server_model_update_state(level_server_0.model, ESP_BLE_MESH_GENERIC_LEVEL_STATE, &state);
                state.gen_onoff.onoff = lightness ? LED_ON : LED_OFF;
                esp_ble_mesh_server_model_update_state(onoff_server.model, ESP_BLE_MESH_GENERIC_ONOFF_STATE, &state);
                state.light_ctl_lightness.lightness = lightness;
                esp_ble_mesh_server_model_update_state(ctl_server.model, ESP_BLE_MESH_LIGHT_CTL_LIGHTNESS_STATE, &state);

                // set index 0 led target status
                g_elem_state[0].state.color_type        = COLOR_HSL;
                g_elem_state[0].state.onoff[T_TAR]      = lightness ? LED_ON : LED_OFF;
                g_elem_state[0].state.hue[T_TAR]        = param->value.state_change.hsl_set.hue;
                g_elem_state[0].state.saturation[T_TAR] = param->value.state_change.hsl_set.saturation;
                g_elem_state[0].state.lightness[T_TAR]  = param->value.state_change.hsl_set.lightness;
                g_elem_state[0].state.actual[T_TAR]     = lightness;
                g_elem_state[0].state.linear[T_TAR]     = convert_lightness_actual_to_linear(lightness);

                // update index 0 led status
                user_event(BLE_MESH_APP_EVT_STATE_UPDATE, &g_elem_state[0]);
                break;
            case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_HUE_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_HUE_SET_UNACK:
                /* Light HSL Hue Server Model - Secondary Element */
                ESP_LOGW(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_HUE_SET Or UNACK, hue: %d", param->value.state_change.hsl_hue_set.hue);
                /* Update bound states */
                state.gen_level.level = param->value.state_change.hsl_hue_set.hue - 32768;
                esp_ble_mesh_server_model_update_state(level_server_1.model, ESP_BLE_MESH_GENERIC_LEVEL_STATE, &state);

                // set index 0 led target status
                g_elem_state[0].state.color_type = COLOR_HSL;
                g_elem_state[0].state.hue[T_TAR] = param->value.state_change.hsl_hue_set.hue;

                // update index 0 led status
                user_event(BLE_MESH_APP_EVT_STATE_UPDATE, &g_elem_state[0]);
                break;
            case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SATURATION_SET:
            case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SATURATION_SET_UNACK:
                /* Light HSL Saturation Server Model - Secondary Element */
                ESP_LOGW(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SATURATION_SET Or UNACK, saturation: %d",
                         param->value.state_change.hsl_saturation_set.saturation);

                /* Update bound states */
                state.gen_level.level = param->value.state_change.hsl_saturation_set.saturation - 32768;
                esp_ble_mesh_server_model_update_state(level_server_2.model, ESP_BLE_MESH_GENERIC_LEVEL_STATE, &state);

                // set index 0 led target status
                g_elem_state[0].state.color_type        = COLOR_HSL;
                g_elem_state[0].state.saturation[T_TAR] = param->value.state_change.hsl_saturation_set.saturation;

                // update index 0 led status
                user_event(BLE_MESH_APP_EVT_STATE_UPDATE, &g_elem_state[0]);
                break;
            }
        }
        break;
    default:
        ESP_LOGE(TAG, "Unknown Lighting Server event: 0x%02x", event);
        break;
    }
}

static void example_ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
        esp_ble_mesh_model_cb_param_t *param)
{
    ENTER_FUNC();
    esp_err_t err                         = ESP_OK;
    uint32_t  opcode                      = 0;
    static    bool timeout_retry_flag     = true;
    static    uint8_t timeout_retry_count = 0;

    ESP_LOGI(TAG, "%s: event: 0x%02x", __FUNCTION__, event);

    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT: {
        if (!param->model_operation.model || !param->model_operation.model->op ||
                !param->model_operation.ctx) {
            ESP_LOGE(TAG, "%s: model_operation parameter is NULL", __FUNCTION__);
            return;
        }
        opcode = param->model_operation.opcode;
        switch (opcode) {
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_SET:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_ADD:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_GET:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_GROUP_ADD:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_GROUP_DELETE: {
            ESP_LOGI(TAG, "Fast prov server receives msg, opcode: 0x%04x", opcode);
            struct net_buf_simple buf = {
                .len = param->model_operation.length,
                .data = param->model_operation.msg,
            };
            err = example_fast_prov_server_recv_msg(param->model_operation.model,
                                                    param->model_operation.ctx, &buf);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to handle fast prov client message", __FUNCTION__);
                return;
            }
            break;
        }
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_STATUS:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_STATUS:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_ACK: {
            ESP_LOGI(TAG, "Fast prov client receives msg, opcode: 0x%04x", opcode);
            err = example_fast_prov_client_recv_status(param->model_operation.model,
                    param->model_operation.ctx, param->model_operation.length, param->model_operation.msg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to handle fast prov server message", __FUNCTION__);
                return;
            }
            break;
        }
        case BLE_MESH_VND_MODEL_OP_NEW_BIN_VERSION_NOTIFY:
            ESP_LOGI(TAG, "BLE_MESH_VND_MODEL_OP_NEW_BIN_VERSION_NOTIFY");
        case BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START:
            ESP_LOGI(TAG, "BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START");
        case BLE_MESH_VND_MODEL_OP_GET_CURRENT_VERSION:
            ESP_LOGI(TAG, "BLE_MESH_VND_MODEL_OP_GET_CURRENT_VERSION");
            ble_mesh_ota_server_recv(param->model_operation.model, param->model_operation.ctx,
                                     param->model_operation.opcode, param->model_operation.length, param->model_operation.msg);
            break;
        case BLE_MESH_VND_MODEL_OP_OTA_UPDATE_STATUS:
        case BLE_MESH_VND_MODEL_OP_CURRENT_VERSION_STATUS:
            ESP_LOGI(TAG, "BLE_MESH_VND_MODEL_OP_OTA_UPDATE_STATUS or BLE_MESH_VND_MODEL_OP_CURRENT_VERSION_STATUS");
            ble_mesh_ota_client_recv(param->model_operation.model, param->model_operation.ctx,
                                     param->model_operation.opcode, param->model_operation.length, param->model_operation.msg);
            break;
        case BLE_MESH_VND_MODEL_OP_OTA_RESTART_NOTIFY:
            ESP_LOGI(TAG, "BLE_MESH_VND_MODEL_OP_OTA_RESTART_NOTIFY");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
            break;
        default:
            ESP_LOGI(TAG, "opcode: 0x%04x", param->model_operation.opcode);
            break;
        }
        break;
    }
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_SEND_COMP_EVT, opcode: 0x%04x, err_code: %d", param->model_send_comp.opcode, param->model_send_comp.err_code);
        switch (param->model_send_comp.opcode) {
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_INFO_STATUS:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NET_KEY_STATUS:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_ACK:
        case ESP_BLE_MESH_VND_MODEL_OP_FAST_PROV_NODE_ADDR_STATUS:
            err = example_handle_fast_prov_status_send_comp_evt(param->model_send_comp.err_code,
                    param->model_send_comp.opcode, param->model_send_comp.model, param->model_send_comp.ctx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to handle fast prov status send complete event", __FUNCTION__);
                return;
            }
            break;
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_MODEL_PUBLISH_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_PUBLISH_COMP_EVT, err_code: %d", param->model_publish_comp.err_code);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_CLIENT_RECV_PUBLISH_MSG_EVT, opcode: 0x%04x", param->client_recv_publish_msg.opcode);
        switch (param->client_recv_publish_msg.opcode) {
        case BLE_MESH_VND_MODEL_OP_NEED_OTA_UPDATE_NOTIFY:
            ESP_LOGI(TAG, "BLE_MESH_VND_MODEL_OP_NEED_OTA_UPDATE_NOTIFY");
            ble_mesh_ota_client_recv(param->client_recv_publish_msg.model, param->client_recv_publish_msg.ctx,
                                     param->client_recv_publish_msg.opcode, param->client_recv_publish_msg.length, param->client_recv_publish_msg.msg);
            break;
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT, opcode: 0x%04x, dst: 0x%04x",
                 param->client_send_timeout.opcode, param->client_send_timeout.ctx->addr);
        switch (param->client_send_timeout.opcode) {
        case BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START: {
            ESP_LOGI(TAG, "BLE_MESH_VND_MODEL_OP_OTA_UPDATE_START");
            if (!timeout_retry_flag) {
                timeout_retry_flag = true;
                break;
            }

            if (timeout_retry_count++ >= 3) {
                timeout_retry_count = 0;
                timeout_retry_flag = false;
                break;
            }

            ble_mesh_ota_update_start_t start = {
                .role     = BLE_MESH_OTA_ROLE_BOARD,
                .ssid     = ota_nvs_data.ssid,
                .password = ota_nvs_data.password,
            };
            esp_err_t err = ESP_OK;
            err = ble_mesh_send_ota_update_start(ota_client.model, param->client_send_timeout.ctx->net_idx,
                                                 param->client_send_timeout.ctx->app_idx, param->client_send_timeout.ctx->addr, &start, false);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Send OTA Update Start failed");
            }
            break;
        }
        default:
            err = example_fast_prov_client_recv_timeout(param->client_send_timeout.opcode,
                    param->client_send_timeout.model, param->client_send_timeout.ctx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Faield to resend fast prov client message", __FUNCTION__);
                return;
            }
            break;
        }
        break;
    case ESP_BLE_MESH_MODEL_PUBLISH_UPDATE_EVT:
        ESP_LOGD(TAG, "ESP_BLE_MESH_MODEL_PUBLISH_UPDATE_EVT, model id: 0x%04x", param->server_model_update_state.model->model_id);
        break;
    case ESP_BLE_MESH_SERVER_MODEL_UPDATE_STATE_COMP_EVT:
        ESP_LOGD(TAG, "ESP_BLE_MESH_SERVER_MODEL_UPDATE_STATE_COMP_EVT, result: %d, model id: 0x%04x, type: 0x%02x",
                 param->server_model_update_state.err_code, param->server_model_update_state.model->model_id, param->server_model_update_state.type);
        break;
    default:
        break;
    }
}

static esp_err_t ble_mesh_init(void)
{
    ENTER_FUNC();
    esp_err_t err = ESP_OK;

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_client_callback(example_ble_mesh_config_client_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);
    esp_ble_mesh_register_generic_server_callback(example_ble_mesh_generic_server_cb);
    esp_ble_mesh_register_lighting_server_callback(example_ble_mesh_lighting_server_cb);
    esp_ble_mesh_register_custom_model_callback(example_ble_mesh_custom_model_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err) {
        ESP_LOGE(TAG, "Initializing mesh failed, err: %d", err);
        return err;
    }

    err = esp_ble_mesh_client_model_init(&vnd_models[0]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize fast prov client model");
        return err;
    }

    err = example_fast_prov_server_init(&vnd_models[1]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize fast prov server model");
        return err;
    }

    err = esp_ble_mesh_client_model_init(&vnd_models[2]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ota client");
        return err;
    }

    err = ble_mesh_ota_server_init(&vnd_models[3]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ota server");
        return err;
    }

    err = esp_ble_mesh_set_unprovisioned_device_name("MESH-OTA-TEST");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set unprovisioned device name");
        return err;
    }

    k_delayed_work_init(&send_self_prov_node_addr_timer, example_send_self_prov_node_addr);

    err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable provision ADV|GATT");
        return err;
    }

    /**
     * @brief Set ADV/SCAN TX MAX Power.
     */
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P7);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P7);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P7);

    ESP_LOGI(TAG, "BLE Mesh Node initialized");

    return err;
}

void app_main(void)
{
    esp_err_t err           = ESP_OK;
    uint8_t   restart_count = 0;

    /**
     * @brief Set the log level for serial port printing.
     */
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("BLE_MESH", ESP_LOG_ERROR);

    ESP_LOGI(TAG, "Initializing ...");
    ESP_LOGI(TAG, "Current Binary ID: 0x%04x, Version: %d.%d.%d",
             ota_server.bin_id, ota_server.curr_version, (ota_server.curr_sub_version >> 4) & 0x0f, ota_server.curr_sub_version & 0x0f);

    /**
     * @brief NVS Flash Initialize.
     */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "nvs flash erase ...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /**
     * @brief Open NVS partition for application used.
     */
    err = ble_mesh_nvs_open(&NVS_HANDLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open example nvs");
        return;
    }

    /**
     * @brief Board specific Initialize.
     */
    err = board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Board specific Initialize Failed");
        return;
    }

    /**
     * @brief Continuous power off and restart more than CONFIG_LIGHT_RESTART_COUNT_ROLLBACK times to rollback.
     */
    restart_count = restart_count_get();
    if (restart_count >= CONFIG_LIGHT_RESTART_COUNT_ROLLBACK) {
        ESP_LOGW(TAG, "Rollback to the previous version");
        upgrade_version_rollback(); /**< Version Rollback */
    }

    /**
     * @brief Read ota data from nvs flash.
     */
    err = ble_mesh_nvs_restore(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data,
                               sizeof(ble_mesh_ota_nvs_data_t), NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore ota nvs data, err: %d", err);
    } else {
        ESP_LOGD(TAG, "dev_flag:         0x%04x", ota_nvs_data.dev_flag);
        ESP_LOGD(TAG, "own_addr:         %d", ota_nvs_data.own_addr);
        ESP_LOGD(TAG, "ssid:             0x%04x", ota_nvs_data.ssid);
        ESP_LOGD(TAG, "password:         0x%04x", ota_nvs_data.password);
        ESP_LOGD(TAG, "bin_id:           %d", ota_nvs_data.bin_id);
        ESP_LOGD(TAG, "curr_version:     %d", ota_nvs_data.curr_version);
        ESP_LOGD(TAG, "curr_sub_version: %d", ota_nvs_data.curr_sub_version);
        ESP_LOGD(TAG, "next_version:     %d", ota_nvs_data.next_version);
        ESP_LOGD(TAG, "next_sub_version: %d", ota_nvs_data.next_sub_version);
        ESP_LOGD(TAG, "flag:             0x%04x", ota_nvs_data.flag);
        ESP_LOGD(TAG, "group_addr:       %d", ota_nvs_data.group_addr);
        ESP_LOGD(TAG, "peer_role:        %d", ota_nvs_data.peer_role);
        ESP_LOGD(TAG, "peer_addr:        0x%04x", ota_nvs_data.peer_addr);
        /* Clear OTA Done Flag. Avoid except reboot */
        ESP_LOGD(TAG, "Clear OTA Done Flag");
        ota_nvs_data.dev_flag &= ~BLE_MESH_OTA_UPDATE_DONE;
        ESP_LOGD(TAG, "ota_nvs_data dev_flag: 0x%02x", ota_nvs_data.dev_flag);
        err = ble_mesh_nvs_store(NVS_HANDLE, OTA_NVS_KEY, &ota_nvs_data, sizeof(ble_mesh_ota_nvs_data_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store example nvs data, err: %d", err);
        }
    }

    /**
     * @brief Read Bluetooth Mac Address.
     */
    uint8_t bt_mac[6] = {0};
    esp_read_mac(bt_mac, ESP_MAC_BT);
    memcpy(&ota_nvs_data.ssid, bt_mac + 4, 2);
    ESP_LOGI(TAG, "MAC Address: " MACSTR, MAC2STR(bt_mac));

    /**
     * @brief Initialize Bluetooth.
     */
    err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth Init Failed (err %d)", err);
        return;
    }

    /**
     * @brief Genrate Device UUID.
     */
    ble_mesh_get_dev_uuid(dev_uuid);

    /**
     * @brief Initialize the Bluetooth Mesh Subsystem.
     */
    err = ble_mesh_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth Mesh Init Failed (err %d)", err);
        return;
    }

    /**
     * @brief Continuous power off and restart more than CONFIG_LIGHT_RESTART_COUNT_RESET times to reset the device.
     */
    if (restart_count >= CONFIG_LIGHT_RESTART_COUNT_RESET) {
        ESP_LOGW(TAG, "Erase information saved in flash, Reset this Ble Mesh node");
        esp_ble_mesh_node_local_reset(); /**< reset prov */
    }

    /**
     * @brief Periodically print system information.
     */
    TimerHandle_t timer = xTimerCreate("show_system_info", pdMS_TO_TICKS(10000),
                                       true, NULL, show_system_info_timercb);
    xTimerStart(timer, portMAX_DELAY);

    /**
     * @brief Print Partition Table.
     */
    print_partition_table();

    /**
     * @brief Mark app as valid.
     */
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    esp_ota_img_states_t ota_state = 0x00;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            vTaskDelay(pdMS_TO_TICKS(10 * 1000)); /**< Delay 10s */
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
#endif
}
