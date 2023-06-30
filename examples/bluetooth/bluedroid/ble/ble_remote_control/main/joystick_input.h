#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

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

#define PIN_OUT_BIT_MASK ((1ULL<<PIN_BUTTON_A) | (1ULL<<PIN_BUTTON_B) | (1ULL<<PIN_BUTTON_C) | (1ULL<<PIN_BUTTON_D))

esp_err_t config_joystick_input();

void read_joystick_input(uint8_t *x_axis, uint8_t *y_axis);

esp_err_t deinit_joystick_input();

esp_err_t config_button_input();

void read_button_input(uint8_t *button_in);

esp_err_t deinit_button_input();