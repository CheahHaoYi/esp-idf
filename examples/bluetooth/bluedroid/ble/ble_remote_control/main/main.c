#include <stdio.h>

// IDF includes
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

// Example includes
#include "joystick_input.h"
#include "gap_gatts.h"
#include "hidd.h"

#define HID_DEMO_TAG "HID_REMOTE_CONTROL"

// Press all the buttons that represents the tear down bit mask to demostrate tear down
#define DEMO_TEAR_DOWN 0
#define TEAR_DOWN_BIT_MASK 0b0011 
// Refer to HID report reference defined in hidd.c
    // bit 0 - Button A, bit 1 - Button B, bit 2 - Button C, bit 3 - Button D
    // 0x03 represent pressing Button A and Button B simultaneously
    
const char *DEVICE_NAME = "ESP32 Remote";
bool is_connected = false;
QueueHandle_t input_queue = NULL;

#define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS)

void print_user_input(uint8_t joystick_x, uint8_t joystick_y, 
            uint8_t hat_switch, uint8_t buttons, uint8_t throttle) 
{
    ESP_LOGI(HID_DEMO_TAG, " ");
    ESP_LOGI(HID_DEMO_TAG, "----- Sending user input -----");

    ESP_LOGI(HID_DEMO_TAG, "X: 0x%x (%d), Y: 0x%x (%d), SW: %d, B: %d, Thr: %d", 
                joystick_x, joystick_x, joystick_y, joystick_y, 
                hat_switch, buttons, throttle);

    if (buttons != 0) {
        ESP_LOGI(HID_DEMO_TAG, "Button pressed: %s %s %s %s", 
            (buttons & 0x01) ? "A" : "-",
            (buttons & 0x02) ? "B" : "-",
            (buttons & 0x04) ? "C" : "-",
            (buttons & 0x08) ? "D" : "-");
    }

    ESP_LOGI(HID_DEMO_TAG, " ----- End of user input ----- \n");
}

void joystick_task() 
{
    esp_err_t ret = ESP_OK;
    input_event_t input_event = {0};
    uint8_t x_axis = 0;
    uint8_t y_axis = 0;
    uint8_t hat_switch = 0; // unused in this example
    uint8_t button_in = 0;
    uint8_t throttle = 0; // unused in this example

    while(true){
        DELAY(500);
        
        if (!is_connected) {
            ESP_LOGI(HID_DEMO_TAG, "Not connected, do not send user input yet");
            DELAY(3000);
            continue;
        }

        // HID report values to set is dependent on the HID report map (refer to hidd.c)
        // For this examples, the values to send are
            // x_axis : 8 bit, 0 - 255
            // y_axis : 8 bit, 0 - 255
            // hat switches : 4 bit
            // buttons 1 to 4: 1 bit each, 0 - 1
            // throttle: 8 bit

        // Send report if there are any inputs
        if (xQueueReceive(input_queue, &input_event, 100) == pdTRUE) {

            switch (input_event.input_source) {
            case (INPUT_SOURCE_BUTTON):
                button_in = input_event.input_data;
                ESP_LOGI(HID_DEMO_TAG, "Button input received");
                break;
            case (INPUT_SOURCE_CONSOLE):
                char_to_joystick_input(input_event.input_data, &x_axis, &y_axis);
                read_button_input(&button_in);
                ESP_LOGI(HID_DEMO_TAG, "Console input received");
                break;
            default:
                ESP_LOGE(HID_DEMO_TAG, "Unknown input source, source number %d", input_event.input_source);
                break;
            }

            set_hid_report_values(x_axis, y_axis, button_in, hat_switch, throttle);
            print_user_input(x_axis, y_axis, hat_switch, button_in, throttle);
            ret = send_user_input();
        } 
        // Alternatively, to simply poll user input can do:
        // read_joystick_input(&x_axis, &y_axis);
        // read_button_input(&button_in);
      
        if (ret != ESP_OK) {
            ESP_LOGE(HID_DEMO_TAG, "Error sending user input, code = %d", ret);
        }

#ifdef DEMO_TEAR_DOWN
        if (button_in == TEAR_DOWN_BIT_MASK) {
            ESP_LOGI(HID_DEMO_TAG, "Tear down button sequence pressed, tear down connection and gpio");
            break;
        }
#endif

    }
}

esp_err_t config_ble()
{
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);

    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s initialize controller failed\n", __func__);
        return ESP_FAIL;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s enable controller failed\n", __func__);
        return ESP_FAIL;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s init bluedroid failed\n", __func__);
        return ESP_FAIL;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s init bluedroid failed\n", __func__);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t tear_down()
{
    deinit_joystick_input();
    deinit_button_input();
    deinit_gap_gatts();

    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    nvs_flash_erase();

    return ESP_OK;
}

void app_main(void)
{
    esp_err_t ret;

    ret = config_joystick_input();
    if (ret != ESP_OK) {
        ESP_LOGE(HID_DEMO_TAG, "%s config joystick failed\n", __func__);
        return;
    }
    ret = config_button_input();
    if (ret != ESP_OK) {
        ESP_LOGE(HID_DEMO_TAG, "%s config button failed\n", __func__);
        return;
    }

    ret = config_ble();
    if (ret != ESP_OK) {
        ESP_LOGE(HID_DEMO_TAG, "%s config ble failed\n", __func__);
        return;
    }
    ESP_LOGI(HID_DEMO_TAG, "BLE configured");

    esp_ble_gap_register_callback(gap_event_callback);
    esp_ble_gatts_register_callback(gatts_event_callback);
    ESP_LOGI(HID_DEMO_TAG, "GAP and GATTS Callbacks registered");

    gap_set_security();
    ESP_LOGI(HID_DEMO_TAG, "Security set");

    // Trigger ESP_GATTS_REG_EVT (see gap_gatts.c and hidd.c)
    esp_ble_gatts_app_register(APP_ID_HID);
    
    ret = esp_ble_gatt_set_local_mtu(500);
    if (ret){
        ESP_LOGE(HID_DEMO_TAG, "set local  MTU failed, error code = %x", ret);
    }

    // Create tasks and queue to handle input events
    input_queue = xQueueCreate(10, sizeof(input_event_t));
    xTaskCreate(console_read_joystick_input, "console_read_joystick_input", 2048, NULL, tskIDLE_PRIORITY, NULL);

    // Main joystick task
    joystick_task(); 
    // Alternatively:
    // xTaskCreate(joystick_task, "joystick_task", 2048, NULL, 5, NULL);

    // Tear down, after returning from joystick_task()
    tear_down();

    ESP_LOGI(HID_DEMO_TAG, "End of joystick demo, restarting in 5 seconds");
    DELAY(5000);
    esp_restart();
}
