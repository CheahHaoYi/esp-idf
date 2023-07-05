#pragma once

#include <stdint.h>
#include <ctype.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"


#include "hidd.h"

#define ADC_UNIT ADC_UNIT_1

#define JOYSTICK_IN_X_ADC_CHANNEL ADC_CHANNEL_0
#define JOYSTICK_IN_Y_ADC_CHANNEL ADC_CHANNEL_1

/**
 * ADC_CHANNEL_2 = GPIO 2 for ESP32C3
*/
#define ADC_BITWIDTH       ADC_BITWIDTH_DEFAULT
#define ADC_ATTEN          ADC_ATTEN_DB_11

#define ADC_RAW_MAX 4095

#define PIN_BUTTON_A GPIO_NUM_4
#define PIN_BUTTON_B GPIO_NUM_5
#define PIN_BUTTON_C GPIO_NUM_6
#define PIN_BUTTON_D GPIO_NUM_7

#define BUTTON_PIN_BIT_MASK ((1ULL<<PIN_BUTTON_A) | (1ULL<<PIN_BUTTON_B) | (1ULL<<PIN_BUTTON_C) | (1ULL<<PIN_BUTTON_D))

#define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS)

// 10% threshold to send joystick input event when using external hardware
#define JOYSTICK_THRESHOLD (UINT8_MAX / 10)

// 30 milliseconds debounce time
#define DEBOUNCE_TIME_US 30000

enum {
    INPUT_SOURCE_BUTTON = 0,
    INPUT_SOURCE_CONSOLE = 1,
    INPUT_SOURCE_JOYSTICK = 2,
};

typedef struct {
    uint8_t input_source;
    uint8_t data_button;
    uint8_t data_console;
    uint8_t data_joystick_x;
    uint8_t data_joystick_y;
} input_event_t;

esp_err_t config_joystick_input(void);

void read_joystick_input(uint8_t *x_axis, uint8_t *y_axis);

esp_err_t deinit_joystick_input(void);

esp_err_t config_button_input(void);

void read_button_input(uint8_t *button_in);

esp_err_t deinit_button_input(void);

void console_read_joystick_input(void *args);

void char_to_joystick_input(uint8_t user_input, uint8_t *x_axis, uint8_t *y_axis);

void ext_hardware_joystick(void *args);
