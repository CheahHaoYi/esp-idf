// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "errno.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp32/rom/rtc.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

#include "ble_mesh_example_nvs.h"
#include "app_utils.h"

/* For OLD-WiFi-Mesh light Upgrade */
#ifndef CONFIG_UPGRADE_FIRMWARE_FLAG
#define CONFIG_UPGRADE_FIRMWARE_FLAG   "** MUPGRADE_FIRMWARE_FLAG **"
#endif

#define UPGRADE_FIRMWARE_FLAG           CONFIG_UPGRADE_FIRMWARE_FLAG
#define UPGRADE_FIRMWARE_FLAG_SIZE      32
#define UPGRADE_STORE_RESTART_COUNT_KEY "upgrade_count" /**< RollBack Restart Counter */

#define DEVICE_RESTART_TIMEOUT_MS       (6000)
#define DEVICE_STORE_RESTART_COUNT_KEY  "restart_count"

extern nvs_handle_t NVS_HANDLE;
static const char *TAG = "app_utils";

void restart_count_erase_timercb(void *timer)
{
    if (!xTimerStop(timer, portMAX_DELAY)) {
        ESP_LOGE(TAG, "xTimerStop timer: %p", timer);
    }

    if (!xTimerDelete(timer, portMAX_DELAY)) {
        ESP_LOGE(TAG, "xTimerDelete timer: %p", timer);
    }

    ble_mesh_nvs_erase(NVS_HANDLE, DEVICE_STORE_RESTART_COUNT_KEY);
    ESP_LOGD(TAG, "Erase restart count");
}

int restart_count_get(void)
{
    esp_err_t    ret                 = ESP_OK;
    uint32_t     restart_count       = 0;
    static       TimerHandle_t timer = NULL;
    RESET_REASON reset_reason        = rtc_get_reset_reason(0);

    ret = ble_mesh_nvs_restore(NVS_HANDLE, DEVICE_STORE_RESTART_COUNT_KEY, &restart_count, sizeof(uint32_t), NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "restore %s failed, err: %d", DEVICE_STORE_RESTART_COUNT_KEY, ret);
    }

    if (timer) {
        return restart_count;
    }

    /**< If the device restarts within the instruction time,
         the event_mdoe value will be incremented by one */
    if (reset_reason == POWERON_RESET || reset_reason == RTCWDT_RTC_RESET) {
        restart_count++;
        ESP_LOGI(TAG, "restart count: %d", restart_count);
    } else {
        restart_count = 1;
        ESP_LOGW(TAG, "restart reason: %d", reset_reason);
    }

    ret = ble_mesh_nvs_store(NVS_HANDLE, DEVICE_STORE_RESTART_COUNT_KEY, &restart_count, sizeof(uint32_t));
    UTILS_ERROR_CHECK(ret != ESP_OK, ret, "Save the number of restarts within the set time");

    timer = xTimerCreate("restart_count_erase", DEVICE_RESTART_TIMEOUT_MS / portTICK_RATE_MS,
                         false, NULL, restart_count_erase_timercb);
    UTILS_ERROR_CHECK(!timer, ret, "xTaskCreate, timer: %p", timer);

    xTimerStart(timer, portMAX_DELAY);

    return restart_count;
}

bool restart_is_exception(void)
{
    esp_err_t ret                      = ESP_OK;
    ssize_t coredump_len               = 0;
    esp_partition_iterator_t part_itra = NULL;

    part_itra = esp_partition_find(ESP_PARTITION_TYPE_DATA,
                                   ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    UTILS_ERROR_CHECK(!part_itra, false, "<%s> esp_partition_find fail", esp_err_to_name(ret));

    const esp_partition_t *coredump_part = esp_partition_get(part_itra);
    UTILS_ERROR_CHECK(!coredump_part, false, "<%s> esp_partition_get fail", esp_err_to_name(ret));


    ret = esp_partition_read(coredump_part, sizeof(ssize_t), &coredump_len, sizeof(ssize_t));
    UTILS_ERROR_CHECK(ret, false, "<%s> esp_partition_read fail", esp_err_to_name(ret));

    if (coredump_len <= 0) {
        return false;
    }

    /**< erase all coredump partition */
    ret = esp_partition_erase_range(coredump_part, 0, coredump_part->size);
    UTILS_ERROR_CHECK(ret, false, "<%s> esp_partition_erase_range fail", esp_err_to_name(ret));

    return true;
}

/**
 * @brief Periodically print system information.
 */
void show_system_info_timercb(void *timer)
{
    uint8_t bt_mac[6] = {0};

    esp_read_mac(bt_mac, ESP_MAC_BT);

    ESP_LOGI(TAG, "System information, self mac: " MACSTR ", free heap: %u, minimum free heap: %u",
             MAC2STR(bt_mac), esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
}

esp_err_t upgrade_version_rollback()
{
    esp_err_t ret                        = ESP_OK;
    const     esp_partition_t *partition = NULL;

#ifdef CONFIG_UPGRADE_VERSION_ROLLBACK_FACTORY

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                         ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);

#endif /**< CONFIG_UPGRADE_VERSION_ROLLBACK_FACTORY */

    if (partition == NULL) {
        partition = esp_ota_get_next_update_partition(NULL);
    }

    ret = esp_ota_set_boot_partition(partition);
    UTILS_ERROR_CHECK(ret != ESP_OK, ret, "esp_ota_set_boot_partition failed!");

    ESP_LOGI(TAG, "The next reboot will fall back to the previous version");

    return ESP_OK;
}

#ifdef CONFIG_UPGRADE_VERSION_ROLLBACK_RESTART

static void upgrade_count_erase_timercb(void *timer)
{
    if (!xTimerStop(timer, portMAX_DELAY)) {
        ESP_LOGE(TAG, "xTimerStop timer: %p", timer);
    }

    if (!xTimerDelete(timer, portMAX_DELAY)) {
        ESP_LOGE(TAG, "xTimerDelete timer: %p", timer);
    }

    ble_mesh_nvs_erase(NVS_HANDLE, UPGRADE_STORE_RESTART_COUNT_KEY);
    ESP_LOGV(TAG, "erase restart count");
}

static bool restart_trigger()
{
    esp_err_t     ret                  = ESP_OK;
    TimerHandle_t timer                = NULL;
    uint32_t      restart_count        = 0;
    uint32_t      restart_count_lenght = sizeof(uint32_t);
    RESET_REASON  reset_reason         = rtc_get_reset_reason(0);

    if (reset_reason == POWERON_RESET || reset_reason == RTCWDT_RTC_RESET
            || reset_reason == DEEPSLEEP_RESET || reset_reason == RTCWDT_BROWN_OUT_RESET) {
        restart_count = 1;
    } else {
        ret = ble_mesh_nvs_restore(NVS_HANDLE, UPGRADE_STORE_RESTART_COUNT_KEY, &restart_count, restart_count_lenght, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "restore %s failed, err: %d", UPGRADE_STORE_RESTART_COUNT_KEY, ret);
        }
        restart_count++;
        ESP_LOGW(TAG, "upgrade restart reason: %d, count: %d", reset_reason, restart_count);
    }

    /**< If the device restarts within the instruction time,
    the event_mdoe value will be incremented by one */
    ret = ble_mesh_nvs_store(NVS_HANDLE, UPGRADE_STORE_RESTART_COUNT_KEY, &restart_count, sizeof(uint32_t));
    UTILS_ERROR_CHECK(ret != ESP_OK, ret, "Save the number of restarts within the set time");

    timer = xTimerCreate("restart_count_erase", CONFIG_UPGRADE_VERSION_ROLLBACK_RESTART_TIMEOUT / portTICK_RATE_MS,
                         false, NULL, upgrade_count_erase_timercb);
    UTILS_ERROR_CHECK(!timer, false, "xTaskCreate, timer: %p", timer);

    xTimerStart(timer, 0);

    if (restart_count < CONFIG_UPGRADE_VERSION_ROLLBACK_RESTART_COUNT) {
        ret = false;
    } else {
        ble_mesh_nvs_erase(NVS_HANDLE, UPGRADE_STORE_RESTART_COUNT_KEY);
        ret = true;
    }

    return ret;
}

static void upgrade_version_rollback_task(void *arg)
{
    if (restart_trigger() && upgrade_version_rollback() == ESP_OK) {
        esp_restart();
    }

    ESP_LOGD(TAG, "version_rollback_task exit");

    vTaskDelete(NULL);
}

#endif /**< CONFIG_UPGRADE_VERSION_ROLLBACK_RESTART */

__attribute((constructor)) esp_err_t upgrade_partition_switch()
{
    esp_err_t err = ESP_OK;
    const volatile uint8_t firmware_flag[UPGRADE_FIRMWARE_FLAG_SIZE] = UPGRADE_FIRMWARE_FLAG;

    (void)firmware_flag;
    ESP_LOGD(TAG, "Add an identifier to the firmware: %s", firmware_flag);

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "nvs_flash_erase...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = ble_mesh_nvs_open(&NVS_HANDLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open example nvs");
        return ESP_FAIL;
    }

#ifdef CONFIG_UPGRADE_VERSION_ROLLBACK_RESTART

    xTaskCreate(upgrade_version_rollback_task, "upgrade_version_rollback", 4 * 1024,
                NULL, 5, NULL);

#endif /**< CONFIG_UPGRADE_VERSION_ROLLBACK_RESTART */

    return ESP_OK;
}

esp_err_t print_partition_table()
{
    ESP_LOGD(TAG, "Partition Table:");
    ESP_LOGD(TAG, "## Label            Usage          Type ST Offset   Length");

    uint8_t i = 0;
    const char *partition_usage = "test";
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *partition = esp_partition_get(it);

        /* valid partition table */
        switch (partition->type) {
        case PART_TYPE_APP: /* app partition */
            switch (partition->subtype) {
            case PART_SUBTYPE_FACTORY: /* factory binary */
                partition_usage = "factory app";
                break;
            case PART_SUBTYPE_TEST: /* test binary */
                partition_usage = "test app";
                break;
            default:
                /* OTA binary */
                if ((partition->subtype & ~PART_SUBTYPE_OTA_MASK) == PART_SUBTYPE_OTA_FLAG) {
                    partition_usage = "OTA app";
                } else {
                    partition_usage = "Unknown app";
                }
                break;
            }
            break; /* PART_TYPE_APP */
        case PART_TYPE_DATA: /* data partition */
            switch (partition->subtype) {
            case PART_SUBTYPE_DATA_OTA: /* ota data */
                partition_usage = "OTA data";
                break;
            case PART_SUBTYPE_DATA_RF:
                partition_usage = "RF data";
                break;
            case PART_SUBTYPE_DATA_WIFI:
                partition_usage = "WiFi data";
                break;
            case PART_SUBTYPE_DATA_NVS_KEYS:
                partition_usage = "NVS keys";
                break;
            case PART_SUBTYPE_DATA_EFUSE_EM:
                partition_usage = "efuse";
                break;
            default:
                partition_usage = "Unknown data";
                break;
            }
            break; /* PARTITION_USAGE_DATA */
        default: /* other partition type */
            break;
        }

        /* print partition type info */
        ESP_LOGD(TAG, "%2d %-16s %-16s %02x %02x %08x %08x", i++, partition->label, partition_usage,
                    partition->type, partition->subtype, partition->address, partition->size);
    }

    it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *partition = esp_partition_get(it);

        /* valid partition table */
        switch (partition->type) {
        case PART_TYPE_APP: /* app partition */
            switch (partition->subtype) {
            case PART_SUBTYPE_FACTORY: /* factory binary */
                partition_usage = "factory app";
                break;
            case PART_SUBTYPE_TEST: /* test binary */
                partition_usage = "test app";
                break;
            default:
                /* OTA binary */
                if ((partition->subtype & ~PART_SUBTYPE_OTA_MASK) == PART_SUBTYPE_OTA_FLAG) {
                    partition_usage = "OTA app";
                } else {
                    partition_usage = "Unknown app";
                }
                break;
            }
            break; /* PART_TYPE_APP */
        case PART_TYPE_DATA: /* data partition */
            switch (partition->subtype) {
            case PART_SUBTYPE_DATA_OTA: /* ota data */
                partition_usage = "OTA data";
                break;
            case PART_SUBTYPE_DATA_RF:
                partition_usage = "RF data";
                break;
            case PART_SUBTYPE_DATA_WIFI:
                partition_usage = "WiFi data";
                break;
            case PART_SUBTYPE_DATA_NVS_KEYS:
                partition_usage = "NVS keys";
                break;
            case PART_SUBTYPE_DATA_EFUSE_EM:
                partition_usage = "efuse";
                break;
            default:
                partition_usage = "Unknown data";
                break;
            }
            break; /* PARTITION_USAGE_DATA */
        default: /* other partition type */
            break;
        }

        /* print partition type info */
        ESP_LOGD(TAG, "%2d %-16s %-16s %02x %02x %08x %08x", i++, partition->label, partition_usage,
                    partition->type, partition->subtype, partition->address, partition->size);
    }

    return ESP_OK;
}
