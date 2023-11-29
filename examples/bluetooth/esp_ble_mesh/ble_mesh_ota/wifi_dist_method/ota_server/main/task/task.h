/* task.h - Application example task */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "driver/gpio.h"
#include "stdbool.h"

/* The examples use OTA configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to number with
   the config you want - ie #define OTA_VERSION 1
*/
// #define OTA_VERSION CONFIG_OTA_VERSION
#define OTA_VERSION 2

#define LED_PIN CONFIG_BLINK_GPIO
#define BLINK_PERIOD (OTA_VERSION * 500)

#define LED_NUM 1
#define LED_INDEX   0

// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)
#define LED_DURATION_OTA 2000

#define DELAY_DURATION_OTA_OUTCOME 1000

#define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS)

// RGB intensity from 0 (0%) to 255 (100%)
#define LED_RED() { .red = 255, .green = 0, .blue = 0,}

#define LED_GREEN() { .red = 0, .green = 255, .blue = 0,}

#define LED_BLUE() { .red = 0, .green = 0, .blue = 255,}

#define LED_WHITE() { .red = 255, .green = 255, .blue = 255,}

// Different colors to identify different OTA versions as an example
#if OTA_VERSION == 1
#define TASK_LED_COLOR() LED_RED()

#elif OTA_VERSION == 2
#define TASK_LED_COLOR() LED_GREEN()

#elif OTA_VERSION == 3
#define TASK_LED_COLOR() LED_BLUE()

#else 
#define TASK_LED_COLOR() LED_WHITE()
#endif

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_rgb_t;

/**
 * @brief   Interface to set on-board LED globally
 * @details Used in example to indicate event state
 * 
 * @param[in]   color       RGB color to set
 * @param[in]   duration_ms Duration in milliseconds to set LED
*/
void ota_set_led(led_rgb_t *color, uint32_t duration_ms);

/**
 * @brief   Initialize hardware for task, in this example to blink LED
 *
*/
esp_err_t task_init(void);

/**
 * @brief   Run task
 * @note   The LED control from ota_set_led() takes precedence over the task LED blinks
 *
*/
void task_run(void* arg);
