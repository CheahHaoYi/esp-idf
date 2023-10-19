#include "task.h"

static const char* TAG = "TASK";

// global variable to control led when in OTA state
bool is_led_in_ota_state;
led_rgb_t ota_led_color = {0};
static uint32_t ota_led_on_duration = 0;
// static variable for led blinking to demonstrate different OTA versions
static led_rgb_t task_led_color = TASK_LED_COLOR();

static led_strip_handle_t led_handle;

void ota_set_led(led_rgb_t *color, uint32_t duration_ms) {
    is_led_in_ota_state = true;
    ota_led_color.red = color->red;
    ota_led_color.green = color->green;
    ota_led_color.blue = color->blue;
    ota_led_on_duration = duration_ms;
}

static void set_led(bool is_on)
{
    // OTA led state takes precedence over task led state
    
    if (is_led_in_ota_state) {
        led_strip_set_pixel(
            led_handle, LED_INDEX, 
            ota_led_color.red, ota_led_color.green, ota_led_color.blue);
        led_strip_refresh(led_handle);
        DELAY(ota_led_on_duration);
        is_led_in_ota_state = false;

    } else if (is_on) {
        led_strip_set_pixel(
            led_handle, LED_INDEX, 
            task_led_color.red, task_led_color.green, task_led_color.blue);
        led_strip_refresh(led_handle);

    } else {
        // Set all LED off to clear all pixels 
        led_strip_clear(led_handle);
    }
}

esp_err_t task_init(void) 
{
    ESP_LOGI(TAG, "Configuring on board LED");

    led_strip_config_t led_config = {
        .strip_gpio_num = LED_PIN,
        .max_leds = LED_NUM,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_config, &rmt_config, &led_handle));

    return led_strip_clear(led_handle);
}

void task_run(void* arg)
{
    // to control led blinking within the task
    bool is_led_blink = false;

    while (true) {
        is_led_blink = !is_led_blink;
        set_led(is_led_blink);
        DELAY(BLINK_PERIOD);
    }
}
