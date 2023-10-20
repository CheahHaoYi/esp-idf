| Supported Targets | ESP32 | ESP32-C3 | ESP32-C6 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- |

ESP BLE Mesh OTA Client Example
==================================

This example demonstrates how to do Over The Air (OTA) update with BLE Mesh, and the [ota server example](../ota_server) demonstrates how a board running BLE Mesh can be updated.

### 1. Procedures

#### 1.1 Setup
1. Create a new self-signed certificate and key by running `bash setup.sh`, alternatively refer to the [OTA example setup](../../../../system/ota/).
2. Copy the generated certificates to the build directory, `cp server_certs/ca* build`
3. On the client project side, run `idf.py menuconfig` to enter the WIFI SSID and Password, or choose the option to read from `stdin`
4. Flash both the client and server example code on 2 separate boards
5. (Optional) On the server project side, change the `OTA_VERSION` macro (via `ota.h` or `idf.py menuconfig`), so that the board running server program will flash a different LED color after a successful OTA update
6. Start the HTTPS server on a separate terminal
    - (Reccomended) run `python python pytest_simple_ota.py build 8070` 
    - run `bash run_server.sh` for an openssl server

#### 1.2 Running the examples
1. Once both examples are flashed, the boards will enter provisioning immediately
2. Press the boot button on the board running the client example to send the credentials to the board running the server example
3. If the transfer is sucessful, the board running the server example will connect to WIFI and download the firmware update
4. If the firmware update download is sucessful, the board will restart and run the updated firmware.

### 2. Results

#### 2.1 Provisioning

```
I (653) EXAMPLE: ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT, err_code 0
I (663) EXAMPLE: ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT, err_code 0
I (673) EXAMPLE: ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT, err_code 0
I (673) EXAMPLE: ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT, err_code 0
I (683) EXAMPLE: ESP BLE Mesh Provisioner initialized
I (693) main_task: Returned from app_main()
I (13453) EXAMPLE: ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT
I (13453) Device address: 34 85 18 99 ac c6 
I (13453) EXAMPLE: Address type 0x00, adv type 0x03
I (13453) Device UUID: 32 10 34 85 18 99 ac c6 00 00 00 00 00 00 00 00 
I (13463) EXAMPLE: oob info 0x0000, bearer PB-ADV
I (13473) EXAMPLE: ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT, bearer PB-ADV
I (13473) EXAMPLE: ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT, err_code 0
I (15343) EXAMPLE: node_index 0, primary_addr 0x0005, element_num 1, net_idx 0x000
I (15343) uuid: 32 10 34 85 18 99 ac c6 00 00 00 00 00 00 00 00 
I (15353) EXAMPLE_NVS: Store, key "vendor_client", length 4
I (15353) EXAMPLE_NVS: Store, data: 05 00 00 00 
I (15363) EXAMPLE: ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT, err_code 0
I (15373) EXAMPLE: Node 0 name NODE-00
I (15793) EXAMPLE: Config client, err_code 0, event 0, addr 0x0005, opcode 0x8008
I (15793) Composition data: e5 02 00 00 00 00 0a 00 03 00 00 00 01 01 00 00 
I (15803) Composition data: e5 02 01 00 
I (15803) EXAMPLE: ********************** Composition Data Start **********************
I (15813) EXAMPLE: * CID 0x02e5, PID 0x0000, VID 0x0000, CRPL 0x000a, Features 0x0003 *
I (15823) EXAMPLE: * Loc 0x0000, NumS 0x01, NumV 0x01 *
I (15823) EXAMPLE: * SIG Model ID 0x0000 *
I (15833) EXAMPLE: * Vendor Model ID 0x0001, Company ID 0x02e5 *
I (15843) EXAMPLE: *********************** Composition Data End ***********************
I (15853) EXAMPLE: ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT, err_code 0
I (16083) EXAMPLE: Config client, err_code 0, event 1, addr 0x0005, opcode 0x0000
I (16163) EXAMPLE: Config client, err_code 0, event 1, addr 0x0005, opcode 0x803d
W (16173) EXAMPLE: example_ble_mesh_config_client_cb, Provision and config successfully
I (17353) EXAMPLE: ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT, bearer PB-ADV, reason 0x00


```
#### 2.2 OTA Progress Update

> Note: for this example, the boot button triggers the credential transfer

```
I (23163) EXAMPLE: Sent OTA credentials
I (23163) EXAMPLE_NVS: Store, key "vendor_client", length 4
I (23163) EXAMPLE_NVS: Store, data: 05 00 00 00 
I (23183) EXAMPLE: Send 0xc202e5
I (23183) EXAMPLE: Send 0xc302e5
I (23183) EXAMPLE: Send 0xc402e5
I (23183) EXAMPLE: Send 0xc502e5
I (30273) EXAMPLE: Received ota update, OTA size: 28672
I (31253) EXAMPLE: Received ota update, OTA size: 56320
I (32213) EXAMPLE: Received ota update, OTA size: 84992
I (33183) EXAMPLE: Received ota update, OTA size: 110592
I (34213) EXAMPLE: Received ota update, OTA size: 138240
I (35293) EXAMPLE: Received ota update, OTA size: 163840
I (36203) EXAMPLE: Received ota update, OTA size: 192512
I (37203) EXAMPLE: Received ota update, OTA size: 220160
I (38173) EXAMPLE: Received ota update, OTA size: 245760
```
