#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "driver/gpio.h"
#include "stdbool.h"

#define LED_PIN CONFIG_BLINK_GPIO
#define BLINK_PERIOD CONFIG_BLINK_PERIOD

#define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS)

// RGB intensity from 0 (0%) to 255 (100%)
// Different colors to identify different OTA versions as an example
#if CONFIG_OTA_VERSION == 1
#define LED_RED_INTENSITY 100
#define LED_GREEN_INTENSITY 0
#define LED_BLUE_INTENSITY 0

#elif CONFIG_OTA_VERSION == 2
#define LED_RED_INTENSITY 0
#define LED_GREEN_INTENSITY 100
#define LED_BLUE_INTENSITY 0

#elif CONFIG_OTA_VERSION == 3
#define LED_RED_INTENSITY 0
#define LED_GREEN_INTENSITY 0
#define LED_BLUE_INTENSITY 100

#else 
#define LED_RED_INTENSITY 100
#define LED_GREEN_INTENSITY 100
#define LED_BLUE_INTENSITY 100
#endif

esp_err_t task_init(void);

void task_run(void* arg);
