| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-H2 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- |

# BLE Mesh OTA Server Example

(See the README.md file in the upper level 'examples' directory for more information about examples.)

This example demonstrate how OTA can be done via BLE

## How to Use Example

Before project configuration and build, be sure to set the correct chip target using `idf.py set-target <chip_name>`.

### Hardware Required

* 2 development boards with Espressif SoC (e.g., ESP32-DevKitC, ESP-WROVER-KIT, etc.)
* A USB cable for Power supply and programming

### Configure the Project

Open the project configuration menu (`idf.py menuconfig`).

In the `Example Task Configuration` menu:

* Select the OTA version (or modify the `OTA_VERSION` macro in the [source code](/main/task/task.h))

### Build and Flash

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

Flash the OTA server example onto one of the dev board
Flash the OTA client example onto the other dev board


## Example Output

Initialization & Provisioning
```
 (594) main_task: Calling app_main()
I (624) BLE_INIT: BT controller compile version [85b425c]
I (624) phy_init: phy_version 600,8dd0147,Mar 31 2023,16:34:12
I (664) BLE_INIT: Bluetooth MAC: 34:85:18:99:ac:c6

I (754) MESH: Intialization of provisioning capabilities and internal data information done
I (764) MESH: Set the unprovisioned device name completion done
I (764) MESH: BLE Mesh Node initialized
I (764) BLE_MESH_OTA: Setup Done, run demo task, ready to receive credentials from client
I (774) TASK: Configuring on board LED
I (774) gpio: GPIO[48]| InputEn: 0| OutputEn: 1| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0 
I (784) BLE_MESH_OTA: Current partition address: factory
I (794) BLE_MESH_OTA: Running partition address: factory
I (794) BLE_MESH_OTA: Next partition address: ota_0
I (804) BLE_MESH_OTA: Example OTA Version: 2
I (804) MESH: BLE Mesh Link established
I (2474) MESH: Provisioning done
I (2724) MESH: BLE Mesh Link closed
I (3204) MESH: Config AppKey Get
I (3344) MESH: SIG Model App Get
```

After receiving credential from BLE Client
```
I (11064) CONNECTION-WIFI: connected to AP
I (11064) OTA: ********** Partition Hash Info Start **********
I (11074) OTA: SHA-256 for bootloader: 
I (11084) OTA: fd 04 f4 da 0f 36 e2 a5 de c9 3f 30 59 79 ee b6 
I (11084) OTA: d5 7a fb cc 98 7e da 4d 59 5e 60 dd e3 b3 f1 4b 
I (11224) OTA: SHA-256 for current firmware: 
I (11224) OTA: 31 d3 6a 6a 72 03 2c e9 0a 09 af d8 43 df 0e 40 
I (11224) OTA: 27 aa 76 98 af f4 33 5c 93 56 ff 1e e9 2a 3b 20 
I (11234) OTA: ********** Partition Hash Info End **********
I (11234) OTA: Starting OTA Update
I (11244) OTA: OTA started
I (11254) wifi:<ba-add>idx:1 (ifx:0, 74:11:b2:74:1e:a0), tid:3, ssn:0, winSize:64
I (11734) OTA: Connected to server
I (11734) esp_https_ota: Starting OTA...
I (11734) esp_https_ota: Writing to partition subtype 16 at offset 0x210000
I (11744) OTA: Reading Image Description
I (11774) OTA: ********** Incoming OTA App Info Start **********
I (11774) OTA: App Name: ble_mesh_ota
I (11774) OTA: App Version: 050053ef81-dirty
I (11774) OTA: App IDF Version: v5.2-dev-544-g54576b7528-dirty
I (11784) OTA: App Compile Date/Time: 	 Oct 19 2023 /	15:42:56
I (11794) OTA: ********** Incoming OTA App Info End **********
I (11794) OTA: OTA Progress Timer created
I (11804) OTA: OTA Image size, from OTA server: 1555552, from MESH client: 1555072
I (11814) OTA: Verifying chip id of new image: 9
I (11874) OTA: Image bytes read: 1024, status: in progress
I (11874) OTA: Image bytes read: 2048, status: in progress
I (11874) OTA: Image bytes read: 3072, status: in progress
I (11934) OTA: Image bytes read: 4096, status: in progress
I (11994) OTA: Image bytes read: 5120, status: in progress
I (11994) OTA: Image bytes read: 6144, status: in progress

```

## Troubleshooting

> Work in progress
