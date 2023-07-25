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

#ifndef _BLE_MESH_OTA_UTILITY_H_
#define _BLE_MESH_OTA_UTILITY_H_

#include "esp_err.h"
#include "esp_ota_ops.h"

enum {
    EVT_OTA_RECV_START = 100,
    EVT_OTA_SEND_START,
    EVT_OTA_RECV_SUCCESS,
    EVT_OTA_SEND_SUCCESS,
    EVT_OTA_RECV_FAILED,
    EVT_OTA_SEND_FAILED,
};
// #define FUNC_TRACE 1

#ifdef FUNC_TRACE
    #define ENTER_FUNC() ESP_LOGI(TAG, "enter %s, %d", __FUNCTION__, __LINE__)
    #define EXIT_FUNC()  ESP_LOGI(TAG, "exit %s, %d", __FUNCTION__, __LINE__)
#else
    #define ENTER_FUNC()
    #define EXIT_FUNC()
#endif

/**
 * @brief
 *
 * @return esp_err_t
 */
esp_err_t ble_mesh_ota_start(void);

#endif /* _BLE_MESH_OTA_UTILITY_H_ */
