#include "mesh.h"

static const char *TAG = "MESH";
static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = DEVICE_UUID;

extern void trigger_ota(void);

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

static esp_ble_mesh_model_t bt_sig_models[] = {
    // Define configuration server model
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

// Vendor Model Context
static esp_ble_mesh_model_op_t vendor_operations[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SEND, VND_MSG_MIN_LEN), 
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_PW_TRSF, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SSID_TRSF, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_OTA_URL_TRSF, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_OTA_SIZE_TRSF, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_OTA_PROGRESS, VND_MSG_MIN_LEN),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(
    CID_ESP, // Company ID 
    ESP_BLE_MESH_VND_MODEL_ID_SERVER, // Model ID 
    vendor_operations, // Supported operations
    NULL,   // Publication context 
    NULL),  // User data context
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

static esp_ble_mesh_msg_ctx_t mesh_client_ctx = {
    .send_ttl = DEFAULT_TTL,
    .send_rel = MSG_TO_SEND_RELIABLY,
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
    case ESP_BLE_MESH_NODE_PROV_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "Disable node provisioning functionality completion");
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
    static uint8_t received_credentials_flag = 0;
    esp_err_t err;

    switch (opcode) {
    case ESP_BLE_MESH_VND_MODEL_OP_SSID_TRSF:
        ESP_LOGI(TAG, "Received SSID Transfer Request");
        err = set_wifi_ssid(model_op->msg, model_op->length);
        received_credentials_flag |= (err == ESP_OK) ? RECEIVE_SSID_FLAG : 0;
        break;
    case ESP_BLE_MESH_VND_MODEL_OP_PW_TRSF:
        ESP_LOGI(TAG, "Received Password Transfer Request");
        err = set_wifi_password(model_op->msg, model_op->length);
        received_credentials_flag |= (err == ESP_OK) ? RECEIVE_PW_FLAG : 0;
        break;
    case ESP_BLE_MESH_VND_MODEL_OP_OTA_URL_TRSF:
        ESP_LOGI(TAG, "Received OTA URL Transfer Request");
        err = set_ota_url(model_op->msg, model_op->length);
        received_credentials_flag |= (err == ESP_OK) ? RECEIVE_OTA_URL_FLAG : 0;
        break;
    case ESP_BLE_MESH_VND_MODEL_OP_OTA_SIZE_TRSF:
        ESP_LOGI(TAG, "Received OTA Size Transfer Request");
        uint64_t ota_size;
        memcpy(&ota_size, model_op->msg, sizeof(ota_size));
        ESP_LOGI(TAG, "Received Expected OTA size: %" PRIu64, ota_size);
        err = set_expected_ota_size(ota_size);
        received_credentials_flag |= (err == ESP_OK) ? RECEIVE_OTA_SIZE_FLAG : 0;
        break;
    case ESP_BLE_MESH_VND_MODEL_OP_SEND:
        ESP_LOGI(TAG, "Received Send Message Request");
        uint64_t value;
        memcpy(&value, model_op->msg, sizeof(value));
        ESP_LOGI(TAG, "Received Value: %" PRIu64, value);
        err = ESP_OK;
        break;
    default:
        ESP_LOGE(TAG, "Unhandled Custom Model Operation, opcode: 0x%06" PRIx32, opcode);
        err = ESP_FAIL;
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update information");
        return; 
    }

    if (opcode != ESP_BLE_MESH_VND_MODEL_OP_OTA_SIZE_TRSF) {
        char received_msg[MSG_BUFFER_SIZE] = {0};
        memcpy(received_msg, model_op->msg, model_op->length);
        ESP_LOGI(TAG, "Received Message: %s", received_msg);
    }
    
    if (received_credentials_flag == ALL_RECEIVED_FLAG) {
        ESP_LOGI(TAG, "Received all credentials, ready for OTA");
        led_rgb_t green = LED_GREEN();
        ota_set_led(&green , LED_DURATION_OTA);
        trigger_ota();
        return;
    } 
    
    ESP_LOGI(TAG, "Received partial credentials, waiting for more");
    return;
}

esp_err_t send_ota_size_update(uint64_t ota_size)
{
    esp_ble_mesh_model_t *model = &vendor_models[0];
    esp_ble_mesh_msg_ctx_t *ctx = &mesh_client_ctx;
    uint32_t opcode = ESP_BLE_MESH_VND_MODEL_OP_OTA_PROGRESS;
    uint16_t length = sizeof(ota_size);
    uint8_t *data = (uint8_t *) &ota_size;

    return esp_ble_mesh_server_model_send_msg(model, ctx, opcode, length, data);
}

void update_mesh_client_ctx(esp_ble_mesh_msg_ctx_t *recv_ctx)
{
    mesh_client_ctx.net_idx = recv_ctx->net_idx;
    mesh_client_ctx.app_idx = recv_ctx->app_idx;
    mesh_client_ctx.addr = recv_ctx->addr;

    ESP_LOGI(TAG, "Context of Received Msg");
    ESP_LOGI(TAG, "Net Index: 0x%04x", recv_ctx->net_idx);
    ESP_LOGI(TAG, "App Index: 0x%04x", recv_ctx->app_idx);
    ESP_LOGI(TAG, "Addr: 0x%04x", recv_ctx->addr);
    ESP_LOGI(TAG, "Recv TTL: 0x%04x", recv_ctx->recv_ttl);
}

// Register BLE Mesh callback for user-defined models’ operations 
// Handle message sending and receiving events
static void ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event, esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        ESP_LOGI(TAG, "Received opcode 0x%06" PRIx32, param->model_operation.opcode);
        update_mesh_client_ctx(param->model_operation.ctx);
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


// Note: No deinit function from BLE Mesh Common Components
esp_err_t ble_mesh_deinit(void)
{
    esp_err_t err = esp_ble_mesh_node_prov_disable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable mesh node (err %d)", err);
        return err;
    }

    esp_ble_mesh_deinit_param_t mesh_deinit = {
        .erase_flash = false,
    };

    err = esp_ble_mesh_deinit(&mesh_deinit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize mesh stack (err %d)", err);
        return err;
    }

    #ifdef CONFIG_BT_BLUEDROID_ENABLED

    err = esp_bluedroid_disable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable bluedroid (err %d)", err);
        return err;
    }

    err = esp_bluedroid_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize bluedroid (err %d)", err);
        return err;
    }

    err = esp_bt_controller_disable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable bt controller (err %d)", err);
        return err;
    }

    err = esp_bt_controller_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize bt controller (err %d)", err);
        return err;
    }
    #endif

    #ifdef CONFIG_BT_NIMBLE_ENABLED
    nimble_port_stop();
    err = nimble_port_deinit();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable nimble (err %d)", err);
        return err;
    }
    #endif

    return err;
}

