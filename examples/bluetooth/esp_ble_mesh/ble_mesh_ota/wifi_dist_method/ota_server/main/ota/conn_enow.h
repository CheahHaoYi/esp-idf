#pragma once

#include "esp_err.h"
#include "esp_log.h"

#include "espnow_storage.h"
#include "espnow.h"
#include "esp_wifi.h"
#include "espnow_utils.h"
#include "espnow_ota.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
// #include "freertos/event_groups.h"

#define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS)
#define MAX_WAIT_COUNT 20
#define WAIT_PERIOD (2000 / portTICK_PERIOD_MS)

#define ESPNOW_CHANNEL 2

#define ESPNOW_OTA_TIMEOUT 40000 /**< Timeout for ESPNOW OTA */

esp_err_t enow_init(void);

esp_err_t enow_update(void);


