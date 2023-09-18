#include "mesh.h"

static const char *TAG = "MESH";
extern bool is_in_OTA;
static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x32, 0x10 };

// Provisioning properties and capabilities
static esp_ble_mesh_prov_t node_provision = {
    .uuid = dev_uuid,
};

// Configuration Server Model Context
static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
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
    .default_ttl = DEFAULT_TTL,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(TRASMIT_COUNT, TRANSMIT_INTERVAL),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(TRASMIT_COUNT, TRANSMIT_INTERVAL),
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_pub, ONOFF_MSG_MIN_LEN, ROLE_NODE);
static esp_ble_mesh_gen_onoff_srv_t onoff_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

static esp_ble_mesh_model_t bt_sig_models[] = {
    // Define configuration server model
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub, &onoff_server),
};

// Vendor Model Context
static esp_ble_mesh_model_op_t vendor_operations[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SEND, VND_MSG_MIN_LEN), 
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_PW_TRSF, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SSID_TRSF, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_OTA_URL_TRSF, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(
    CID_ESP, // Company ID 
    ESP_BLE_MESH_VND_MODEL_ID_SERVER, // Model ID 
    vendor_operations, // Supported operations
    NULL, // Publication context ??
    NULL), // User data context
};

// Collection of elements of the node
static esp_ble_mesh_elem_t mesh_elements[] = {
    ESP_BLE_MESH_ELEMENT(
        LOC_DESCR, // Location Descriptor
        bt_sig_models, // Arr of SIG models
        vendor_models // Arr of Vendor models
    ),
};

// Provide data context of Node
static esp_ble_mesh_comp_t node_composition = {
    .cid = CID_ESP, // Company ID
    .elements = mesh_elements, // Collection of node elements
    .element_count = ARRAY_SIZE(mesh_elements),
};

// Provisioning Callback
// Provisioner events and node config events
static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "Intialization of provisioning capabilities and internal data information done");
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Set the unprovisioned device name completion done");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "BLE Mesh Link established");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "BLE Mesh Link closed");
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "Provisioning done");
        break;
    case ESP_BLE_MESH_PROXY_SERVER_CONNECTED_EVT:
        ESP_LOGI(TAG, "Proxy Server connected sucessfully");
        break;
    case ESP_BLE_MESH_PROXY_SERVER_DISCONNECTED_EVT:
        ESP_LOGI(TAG, "Proxy Server disconnected");
        break;
    default:
        // Not all provisioning event are mandatory to be handled
        // Refer to esp_ble_mesh_defs.h for more information
        ESP_LOGI(TAG, "Unhandled Provisioning Event: %d", event);
        break;
    }
    return;
}

/******************************************
 * BLE Mesh Generic On-Off Server Related Functions 
 * ****************************************/

static bool is_intended_receipient(esp_ble_mesh_model_t *model, uint16_t recv_dst)
{
    bool is_unicast_receipient = ESP_BLE_MESH_ADDR_IS_UNICAST(recv_dst);
    bool is_group_receipient = ESP_BLE_MESH_ADDR_IS_GROUP(recv_dst) && 
        esp_ble_mesh_is_model_subscribed_to_group(model, recv_dst);
    bool is_broadcast_receipient = (recv_dst == ESP_BLE_MESH_ADDR_ALL_NODES);
    
    return (is_unicast_receipient || is_group_receipient || is_broadcast_receipient);
}

static void handle_ota(esp_ble_mesh_model_t *model, uint16_t recv_dst, uint8_t is_trigger_OTA)
{   
    if (!is_trigger_OTA) {
        ESP_LOGI(TAG, "Not triggering OTA, ignore");
        return;
    }

    if (!is_intended_receipient(model, recv_dst)) {
        ESP_LOGI(TAG, "Not intended receipient for OTA request, ignore");
        return; 
    }

    ESP_LOGI(TAG, "Received OTA update request");
    ota_update();
    return;
}

static void onoff_server_set(esp_ble_mesh_model_t *model, esp_ble_mesh_msg_ctx_t *context, uint8_t onoff_set_value)
{
    uint32_t opcode = context->recv_op;
    
    // set the present onoff state value
    esp_ble_mesh_gen_onoff_srv_t *onoff_server_data = model->user_data;
    ESP_LOGI(TAG, "Current Value: %d, Received Value: %d", onoff_server_data->state.onoff, onoff_set_value);
    onoff_server_data->state.onoff = onoff_set_value;
    
    // for acknowledged set messages, need to reply generic onoff status back to client
    // refer to Bluetooth Mesh Model Specification
    if (opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
        ESP_LOGI(TAG, "Sending Generic OnOff Status Message");
        esp_ble_mesh_server_model_send_msg(model, context, 
            ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS, 
            sizeof(onoff_server_data->state.onoff), &onoff_server_data->state.onoff);
    }

    // Both ack and unack set messages need to publish new state info to model publish addr
    // refer to Bluetooth Mesh Model Specification
    esp_ble_mesh_model_publish(model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS, 
        sizeof(onoff_server_data->state.onoff), &onoff_server_data->state.onoff, ROLE_NODE);

    // Handle the OTA request
    handle_ota(model, context->recv_dst, onoff_server_data->state.onoff);

    return;
}

static void ble_mesh_generic_onoff_srv_cb(esp_ble_mesh_generic_server_cb_event_t event, esp_ble_mesh_generic_server_cb_param_t *param)
{
    uint32_t opcode_recv = param->ctx.recv_op;
    switch (event) {
    case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
        // Receive Generic Get Message 
        // if get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT");
        if (opcode_recv == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
            ESP_LOGI(TAG, "Sending Generic OnOff Status Message");
            esp_ble_mesh_gen_onoff_srv_t *onoff_server_data = param->model->user_data;
            esp_ble_mesh_server_model_send_msg(param->model, &param->ctx, 
            ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS, 
            sizeof(onoff_server_data->state.onoff), &onoff_server_data->state.onoff);
        }
        break;
    case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
        // Receive Generic Set/Set Unack Message
        // if set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP
        ESP_LOGI(TAG, "Generic Server Receive Set Message Event");
        if (opcode_recv == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
            opcode_recv == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {
            onoff_server_set(param->model, &param->ctx, param->value.set.onoff.onoff);
        }
        break;
    case ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT:
        // Receive Generic Set/Set Unack Messages
        // if set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP
        ESP_LOGI(TAG, "Generic Server State Change Event");
        if (opcode_recv == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
            opcode_recv == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {
            onoff_server_set(param->model, &param->ctx, param->value.state_change.onoff_set.onoff);
        }
        break;
    default:
        // note: if get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP
        // then no event callback for receiving Generic Get Message
        ESP_LOGI(TAG, "Unhandled Generic OnOff Server Event: %d", event);
        break;
    }
    return;
}

/******************************************
 * BLE Mesh Configuration Server Related Functions 
 * ****************************************/

static void process_server_changes(uint32_t opcode, esp_ble_mesh_cfg_server_state_change_t value_received)
{
    switch (opcode) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        ESP_LOGI(TAG, "Config AppKey Get");
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        ESP_LOGI(TAG, "SIG Model App Get");
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
        ESP_LOGI(TAG, "Config Model Subscription Add");
        break;
    default:
        ESP_LOGI(TAG, "Unhandled Configuration Server Operation, opcode: 0x%06" PRIx32, opcode);
        break;
    }
    return;
}


// Configuration Server Callback
// Handle configuration server event
static void ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event, esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        uint32_t opcode = param->ctx.recv_op;
        process_server_changes(opcode, param->value.state_change);
    } else {
        // ESP_BLE_MESH_CFG_SERVER_EVT_MAX is not used
        ESP_LOGI(TAG, "Unhandled Configuration Server Event: %d", event);
    }
    return;
}

/******************************************
 * BLE Mesh Custom Model Related Functions 
 * ****************************************/
static void process_received_opcode(uint32_t opcode, struct ble_mesh_model_operation_evt_param *model_op)
{
    switch (opcode) {
    case ESP_BLE_MESH_VND_MODEL_OP_SSID_TRSF:
        ESP_LOGI(TAG, "Received SSID Transfer Request");
        break;
    case ESP_BLE_MESH_VND_MODEL_OP_PW_TRSF:
        ESP_LOGI(TAG, "Received Password Transfer Request");
        break;
    case ESP_BLE_MESH_VND_MODEL_OP_OTA_URL_TRSF:
        ESP_LOGI(TAG, "Received OTA URL Transfer Request");
        break;
    default:
        ESP_LOGE(TAG, "Unhandled Custom Model Operation, opcode: 0x%06" PRIx32, opcode);
        break;
    }
    char msg[256] = {0};
    memcpy(msg, model_op->msg, model_op->length);
    ESP_LOGI(TAG, "Received Message: %s", msg);

    return;
}


// Register BLE Mesh callback for user-defined models’ operations 
// Handle message sending and receiving events
static void ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event, esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        ESP_LOGI(TAG, "Received opcode 0x%06" PRIx32, param->model_operation.opcode);
        process_received_opcode(param->model_operation.opcode, &param->model_operation);
        break;
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        ESP_LOGI(TAG, "Send %s, opcode: 0x%06" PRIx32 ,
                param->model_send_comp.err_code ? "failed" : "successful",
                param->model_send_comp.opcode);
        break;
    default:
        ESP_LOGI(TAG, "Unhandled Custom Model Event: %d", event);
        break;
    }
    return;
}


esp_err_t ble_mesh_init(void)
{
    esp_err_t err = bluetooth_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s init bluetooth failed", esp_err_to_name(err));
        return ESP_FAIL;
    }
    
    ble_mesh_get_dev_uuid(dev_uuid);

    // Register callbacks
    esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(ble_mesh_config_server_cb);
    esp_ble_mesh_register_generic_server_callback(ble_mesh_generic_onoff_srv_cb);
    esp_ble_mesh_register_custom_model_callback(ble_mesh_custom_model_cb);

    // Initialize BLE_MESH module and Enable provisioning
    err = esp_ble_mesh_init(&node_provision, &node_composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
        return err;
    }

    // Determine the provisioning method
    err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh Node initialized");
    return ESP_OK;
}
