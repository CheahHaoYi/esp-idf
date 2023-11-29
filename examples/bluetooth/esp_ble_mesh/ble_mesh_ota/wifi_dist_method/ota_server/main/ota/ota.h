/* ota.h - OTA update utility */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "string.h"
#include "mesh/mesh.h"

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#ifdef CONFIG_EXAMPLE_USE_CERT_BUNDLE
#include "esp_crt_bundle.h"
#endif

#if CONFIG_EXAMPLE_CONNECT_WIFI
#include "esp_wifi.h"
#endif

#define HASH_LEN 32

#define OTA_URL_SIZE 256

#define OTA_RECV_TIMEOUT CONFIG_EXAMPLE_OTA_RECV_TIMEOUT_MS
#define OTA_UPDATE_INTERVAL CONFIG_EXAMPLE_OTA_UPDATE_INTERVAL_MS

/**
 * @brief   Perform firmware download over HTTPS
 * @note    The firmware download ends based on the firmware size received from HTTPS server header
 *          If the server did not provide the content length header,
 *          The firmware download ends based on the expected firmware size provided by Mesh Client
 * 
 * @note    The firmware update url and expected size must be set before calling this function
 * 
 * @return  ESP_OK on success, any other value indicates error
*/
esp_err_t ota_update(void);

/**
 * @brief   Set the URL for the firmware download
 * 
 * @param   url     URL of the firmware image
 * @param   len     Length of the URL
 * 
 * @return  ESP_OK on success, ESP_ERR_INVALID_ARG if the URL is invalid
 * 
*/
esp_err_t set_ota_url(uint8_t *url, uint16_t len);

/**
 * @brief   Set the expected size of the firmware image
 * 
 * @param   received_size   Size of the firmware image
 * 
 * @return  ESP_OK on success, ESP_ERR_INVALID_ARG if the size is invalid
 * 
*/
esp_err_t set_expected_ota_size(uint64_t received_size);