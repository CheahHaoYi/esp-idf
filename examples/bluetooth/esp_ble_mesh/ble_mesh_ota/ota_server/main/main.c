#include "esp_log.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Example includes
#include "task/task.h"
#include "ota/ota.h"
#include "ota/connection.h"

#include "mesh/mesh.h"

static const char *TAG = "BLE_MESH_OTA";

static SemaphoreHandle_t xSemaphore_wifi = NULL;

void trigger_ota(void)
{
    ESP_LOGI(TAG, "Triggering OTA sequence");
    xSemaphoreGive(xSemaphore_wifi);
}

void get_partition_details(void)
{
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);

    ESP_LOGI(TAG, "Current partition address: %s", configured->label);
    ESP_LOGI(TAG, "Running partition address: %s", running->label);
    ESP_LOGI(TAG, "Next partition address: %s", next->label);
    ESP_LOGI(TAG, "Example OTA Version: %d", OTA_VERSION);
}


esp_err_t update_sequence(void) 
{
    esp_err_t err;
    led_rgb_t color = LED_WHITE();
    ota_set_led(&color, LED_DURATION_OTA);

    err = simple_connect();
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "WiFi connection failed, aborting OTA");
        return err;
    }

    err = ota_update();
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "OTA Update failed");
        return err;
    }

    return err;
}


void app_main()
{
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Semaphore to wait for wifi connection trigger 
    xSemaphore_wifi = xSemaphoreCreateBinary();

    ble_mesh_init();

    ESP_LOGI(TAG, "Setup Done, run demo task, ready to receive credentials from client");
    task_init();
    xTaskCreate(&task_run, "task", 2048, NULL, tskIDLE_PRIORITY, NULL);
    get_partition_details();

    while (true) {
        if (xSemaphoreTake(xSemaphore_wifi, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Start of WiFi and OTA sequence");

            err = update_sequence();

            if (err == ESP_OK) {
                ESP_LOGI(TAG, "OTA Success, restarting in 3 seconds");
                led_rgb_t color_success = LED_GREEN();
                ota_set_led(&color_success, LED_DURATION_OTA);
                break;
            }

            ESP_LOGI(TAG, "OTA Failed, wait for credentials again");
            led_rgb_t color_fail = LED_RED();
            ota_set_led(&color_fail, LED_DURATION_OTA);
        }
    }
    
    ble_mesh_deinit();
    DELAY(3000);
    esp_restart();
}