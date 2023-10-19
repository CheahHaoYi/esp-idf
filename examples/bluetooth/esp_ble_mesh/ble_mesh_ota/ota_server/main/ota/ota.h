/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
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

esp_err_t ota_update(void);

esp_err_t set_ota_url(uint8_t *url, uint16_t len);

esp_err_t set_expected_ota_size(uint64_t received_size);