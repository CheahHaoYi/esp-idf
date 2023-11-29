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

### TCP Method 


### ESP Now Method

## References
[BLE Mesh Technical Overview](https://www.bluetooth.com/bluetooth-resources/bluetooth-mesh-models/)
[ESP IDF BLE Mesh API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp-ble-mesh.html)

