#include "esp_log.h"

#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "iot_button.h"

// Example includes
#include "task/task.h"
#include "ota/ota.h"
#include "ota/connect_wifi.h"
#include "mesh/mesh.h"

static const char *TAG = "BLE_MESH_OTA";

bool is_blink = true; //global variable to control LED blinking

void set_blink(bool to_blink)
{
    is_blink = to_blink;
}

// button related functions to test OTA
button_handle_t button_handle;

void button_callback(void *arg, void *usr_data)
{
    ESP_LOGI(TAG, "BUTTON PRESSED, starting OTA");
    ota_update();
}

void button_init(void)
{
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 0,
        .short_press_time = 0,
        .gpio_button_config = {
            .gpio_num = CONFIG_BOARD_BUTTON_GPIO,
            .active_level = 0,
        },
    };
    
    button_handle = iot_button_create(&button_config);
    iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, button_callback, NULL);
}
// End of button related functions

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

    // ble_mesh_init();
    // // ota_init(); //ignore
    task_init();
    button_init();
    char test_ssid[] = "---";
    char test_password[] = "---";
    
    esp_err_t err_wifi = set_wifi_credentials(test_ssid, test_password);
    if (err_wifi == ESP_OK) {
        ESP_LOGI(TAG, "Credentials set successfully");
    } else {
        ESP_LOGE(TAG, "Failed to set credentials");
    }

    wifi_init();

    esp_err_t ret = wifi_connect();

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Connected to AP");
    } else {
        ESP_LOGE(TAG, "Failed to connect to AP");
    }

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);

    ESP_LOGI(TAG, "Current partition address: %s", configured->label);
    ESP_LOGI(TAG, "Running partition address: %s", running->label);
    ESP_LOGI(TAG, "Next partition address: %s", next->label);
    ESP_LOGI(TAG, "Example OTA Version: %d", CONFIG_OTA_VERSION);
    ESP_LOGI(TAG, "Setup Done, run demo task");
    xTaskCreate(&task_run, "task", 2048, NULL, tskIDLE_PRIORITY, NULL);
}