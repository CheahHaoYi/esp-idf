#include "task.h"

static const char* TAG = "TASK";

extern bool is_blink; // gloabl variable to control LED blinking
bool is_led_on = false;
static led_strip_handle_t led_handle;

static void set_led(bool is_on)
{
    if (is_blink && is_on) {
        led_strip_set_pixel(
            led_handle, 0, 
            LED_RED_INTENSITY, LED_GREEN_INTENSITY, LED_BLUE_INTENSITY);
        // Refresh to send data
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
        .max_leds = 1,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10000000, // 10 Mhz
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_config, &rmt_config, &led_handle));

    return led_strip_clear(led_handle);
}

void task_run(void* arg)
{
    while (true) {
        is_led_on = !is_led_on;
        set_led(is_led_on);
        DELAY(BLINK_PERIOD);
    }
}
