set(COMPONENT_SRCS "model/ble_mesh_ota_client_model.c"
        "model/ble_mesh_ota_model_msg.c"
        "model/ble_mesh_ota_server_model.c"
        "operation/ble_mesh_ota_bearer.c"
        "operation/ble_mesh_ota_op.c"
        "operation/ble_mesh_ota_utility.c")

set(COMPONENT_ADD_INCLUDEDIRS "model/include" "operation/include")

set(COMPONENT_REQUIRES example_nvs bt app_update esp_https_ota)

register_component()
