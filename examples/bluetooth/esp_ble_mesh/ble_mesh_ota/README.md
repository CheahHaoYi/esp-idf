# BLE Mesh and OTA

The intention of the example was to implement an example of doing OTA in a BLE Mesh network.
There were a few implementations considered and here are some elaborations, the thought process and what would be the approach to complete the implementation.

## Background information
BLE Mesh is a many-to-many topological network that's based on Bluetooth Low Energy.

In BLE, there are different models definitions as specified in the [specifications](https://www.bluetooth.com/bluetooth-resources/bluetooth-mesh-models/). The APIs for ESP Mesh models are available on the [programming guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp-ble-mesh.html).

From the Bluetooth Mesh model Overview (Version 1.0):
> Models are specifications for standard software components that, when included, determine what it can do as a mesh device. 

> Models are classified as being either clients, which do not contain state, or servers, which do.

For the OTA portion, 
- the client will be the controller to send information regarding firmware updates
- the server will be the entity getting updated

For the exchange of messages, we will be building on top of the BLE Mesh Vendor Model as provided by Espressif.


## Implementation Tested
The constraint of this example is that the firmware cannot be transferred via BLE Mesh directly, because the throughput of BLE is way too low.

Hence the various approaches for the OTA update involves the following: 
- fetching the firmware onto the BLE Mesh client
- exchanging information via BLE
- switching to another protocol
- transfer firmware to the BLE Mesh Server/ other Mesh nodes
- (still in progress) The other Mesh nodes might switch modes to propagate the firmware further 

### Wifi Distribution method
The high level overview is as such:
- The Client will send the WIFI credentials and some firmware information to the Server.
- With the information received, the Server would connect to WIFI and to download the firmware via HTTPS  

#### Considerations
This approach is very simple and straightforward, just signal for server to download
This approach made a HUGE assumption: that the server node must be within the WIFI range, which defeats the purpose of using BLE Mesh in the first place.

#### References
- [ESP HTTPS OTA API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_https_ota.html) 

### ESP Now Method
Instead of having the server to download the firmware directly via WIFI. The client would first download the firmware, then transfer the firmware to other nodes via ESP-NOW as a OTA initiator.

> Note: currently the implementation is located in the wifi_dist folder example, the code is in the conn_enow.c file, some refactoring needed. 

#### Considerations
From testing, sometimes the OTA responder cannot receive messages from the OTA initiator reliably. It could be that ESP NOW requires both devices to be set on the same channel, more testing is required. The current ESP-NOW OTA API does not allow very fine control of the OTA process. 

#### References
- [ESP NOW OTA example](https://github.com/espressif/esp-now/tree/master/examples/ota)
- [ESP RF coexist](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/coexist.html)


### TCP Method 
Similar to the ESP NOW method, this approach involve sending the firmware from the client to server via TCP packets. The lower level implementation will closely resemble that of the ESP-NOW initiator and responder API. Current implementation faces some issues on firmware download from http client (can refer to the ESP-NOW OTA example for more information)

The implementation involves having the client, as a WIFI STA connecting to a WIFI AP to download the firmware.
The client will then switch mode to SoftAP, and establish connection with the BLE servers (which switch on wifi mode upon receiving signals from the client).

#### Consideration
The current implementation tried to download the firmware via HTTP, one possibility that we can explore is to use the https_ota API to download the firmware instead.

#### References
- [ESP WIFI Station & SoftAP example](https://github.com/espressif/esp-idf/tree/master/examples/wifi/getting_started)
- [ESP WIFI APSTA](https://github.com/espressif/esp-idf/tree/master/examples/wifi/softap_sta)
- [ESP HTTPS OTA](https://github.com/espressif/esp-idf/tree/master/examples/system/ota)

## References
- [BLE Mesh Technical Overview](https://www.bluetooth.com/bluetooth-resources/bluetooth-mesh-models/)
- [ESP IDF BLE Mesh API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp-ble-mesh.html)





