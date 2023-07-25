/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"

#include "light_driver.h"
#include "iot_button.h"

#include "ble_mesh_example_nvs.h"

#include "app_utils.h"
#include "board.h"

#define BTN_ON_OFF          0   /* on/off button */
#define BTN_ACTIVE_LEVEL    0

static const char *TAG  = "board";

static uint32_t dev_on_btn_num = BTN_ON_OFF;

extern nvs_handle_t NVS_HANDLE;
extern void button_event(void);
extern elem_state_t g_elem_state[LIGHT_MESH_ELEM_STATE_COUNT];

void button_tap_cb(void *arg)
{
    ENTER_FUNC();
    button_event();
}

static esp_err_t board_led_init(void)
{
    /**
     * NOTE:
     *  If the module has SPI flash, GPIOs 6-11 are connected to the module’s integrated SPI flash and PSRAM.
     *  If the module has PSRAM, GPIOs 16 and 17 are connected to the module’s integrated PSRAM.
     */
    light_driver_config_t driver_config = {
        .gpio_red        = CONFIG_LIGHT_GPIO_RED,
        .gpio_green      = CONFIG_LIGHT_GPIO_GREEN,
        .gpio_blue       = CONFIG_LIGHT_GPIO_BLUE,
        .gpio_cold       = CONFIG_LIGHT_GPIO_COLD,
        .gpio_warm       = CONFIG_LIGHT_GPIO_WARM,
        .fade_period_ms  = CONFIG_LIGHT_FADE_PERIOD_MS,
        .blink_period_ms = CONFIG_LIGHT_BLINK_PERIOD_MS,
    };

    /**
     * @brief Light driver initialization
     */
    ESP_ERROR_CHECK(light_driver_init(&driver_config));
    light_driver_set_switch(true);

    button_handle_t dev_on_off_btn = iot_button_create(BTN_ON_OFF, BTN_ACTIVE_LEVEL);
    iot_button_set_evt_cb(dev_on_off_btn, BUTTON_CB_TAP, button_tap_cb, &dev_on_btn_num);

    return ESP_OK;
}

esp_err_t board_init(void)
{
    return board_led_init();
}

void board_led_hsl(uint8_t elem_index, uint16_t hue, uint16_t saturation, uint16_t lightness)
{
    ENTER_FUNC();
    static uint16_t last_hue        = 0xFFFF;
    static uint16_t last_saturation = 0xFFFF;
    static uint16_t last_lightness  = 0xFFFF;

    ESP_LOGD(TAG, "hue last state %d, state %d", last_hue, hue);
    ESP_LOGD(TAG, "saturation last state %d, state %d", last_saturation, saturation);
    ESP_LOGD(TAG, "lightness last state %d, state %d", last_lightness, lightness);

    if (last_hue != hue || last_saturation != saturation || last_lightness != lightness ) {
        last_hue        = hue;
        last_saturation = saturation;
        last_lightness  = lightness;

        uint16_t actual_hue        = (float)last_hue / (UINT16_MAX / 360.0);
        uint8_t  actual_saturation = (float)last_saturation / (UINT16_MAX / 100.0);
        uint8_t  actual_lightness  = (float)last_lightness / (UINT16_MAX / 100.0);

        ESP_LOGI(TAG, "hsl: %d, %d, %d operation", actual_hue, actual_saturation, actual_lightness);
        light_driver_set_hsl(actual_hue, actual_saturation, actual_lightness);
    }
}

void board_led_ctl(uint8_t elem_index, uint16_t temperature, uint16_t lightness)
{
    ENTER_FUNC();
    static uint16_t last_temperature = 0xFFFF;
    static uint16_t last_lightness  = 0xFFFF;

    ESP_LOGD(TAG, "temperature last state %d, state %d", last_temperature, temperature);

    if (last_temperature != temperature || last_lightness != lightness ) {
        last_temperature = temperature;
        last_lightness  = lightness;

        // uint16_t actual_temperature = (float)last_temperature / (UINT16_MAX / 100.0);
        uint16_t actual_temperature = last_temperature - 0x0320;
        uint8_t  actual_lightness  = (float)last_lightness / (UINT16_MAX / 100.0);
        ESP_LOGI(TAG, "ctl: %d, %d operation", actual_temperature, actual_lightness);
        light_driver_set_ctl(actual_temperature, actual_lightness);
    }
}

void board_led_temperature(uint8_t elem_index, uint16_t temperature)
{
    ENTER_FUNC();
    static uint16_t last_temperature = 0xFFFF;

    ESP_LOGD(TAG, "temperature last state %d, state %d", last_temperature, temperature);

    if (last_temperature != temperature) {
        last_temperature = temperature;

        // uint16_t actual_temperature = (float)last_temperature / (UINT16_MAX / 100.0);
        uint16_t actual_temperature = last_temperature - 0x0320;
        ESP_LOGI(TAG, "temperature %d %%%d operation", last_temperature, actual_temperature);
        light_driver_set_color_temperature(actual_temperature);
    }
}

/**
 * actual lightness
 */
void board_led_lightness(uint8_t elem_index, uint16_t actual)
{
    ENTER_FUNC();
    static uint16_t last_acual = 0xFFFF;

    ESP_LOGD(TAG, "actual last state %d, state %d", last_acual, actual);

    if (last_acual != actual) {
        last_acual = actual;

        uint16_t actual_lightness = (float)last_acual / (UINT16_MAX / 100.0);
        ESP_LOGI(TAG, "lightness %d %%%d operation", last_acual, actual_lightness);
        light_driver_set_lightness(actual_lightness);
    }
}

/**
 * onoff on/off
 */
void board_led_switch(uint8_t elem_index, uint8_t onoff)
{
    ENTER_FUNC();
    static uint8_t last_onoff = 0xFF;

    ESP_LOGD(TAG, "onoff last state %d, state %d", last_onoff, onoff);

    if (last_onoff != onoff) {
        last_onoff = onoff;
        if (last_onoff) {
            ESP_LOGI(TAG, "onoff %d operation", last_onoff);
            light_driver_set_switch(true);
        } else {
            ESP_LOGI(TAG, "onoff %d operation", last_onoff);
            light_driver_set_switch(false);
        }
    }
}

#define MINDIFF (2.25e-308)

static float bt_mesh_sqrt(float square)
{
    float root, last, diff;

    root = square / 3.0;
    diff = 1;

    if (square <= 0) {
        return 0;
    }

    do {
        last = root;
        root = (root + square / root) / 2.0;
        diff = root - last;
    } while (diff > MINDIFF || diff < -MINDIFF);

    return root;
}

static int32_t bt_mesh_ceiling(float num)
{
    int32_t inum = (int32_t)num;
    if (num == (float)inum) {
        return inum;
    }
    return inum + 1;
}

uint16_t convert_lightness_actual_to_linear(uint16_t actual)
{
    float tmp = ((float) actual / UINT16_MAX);
    return bt_mesh_ceiling(UINT16_MAX * tmp * tmp);
}

uint16_t convert_lightness_linear_to_actual(uint16_t linear)
{
    return (uint16_t)(UINT16_MAX * bt_mesh_sqrt(((float) linear / UINT16_MAX)));
}

int16_t convert_temperature_to_level(uint16_t temp, uint16_t min, uint16_t max)
{
    float tmp = (temp - min) * UINT16_MAX / (max - min);
    return (int16_t) (tmp + INT16_MIN);
}

uint16_t covert_level_to_temperature(int16_t level, uint16_t min, uint16_t max)
{
    float diff = (float) (max - min) / UINT16_MAX;
    uint16_t tmp = (uint16_t) ((level - INT16_MIN) * diff);
    return (uint16_t) (min + tmp);
}

void reset_light_state(void)
{
    ENTER_FUNC();
    uint8_t i = 0;

    while (i < LIGHT_MESH_ELEM_STATE_COUNT) {
#ifdef CONFIG_MESH_MODEL_GEN_ONOFF_SRV
        g_elem_state[i].state.onoff[T_CUR] = GEN_ONOFF_DEFAULT;
        g_elem_state[i].state.onoff[T_TAR] = GEN_ONOFF_DEFAULT;
#endif
#ifdef CONFIG_MESH_MODEL_GEN_LEVEL_SRV
        g_elem_state[i].state.level[T_TAR] = LEVEL_DEFAULT;
        g_elem_state[i].state.level[T_TAR] = LEVEL_DEFAULT;
#endif
#ifdef CONFIG_MESH_MODEL_LIGHTNESS_SRV
        g_elem_state[i].state.actual[T_CUR] = LIGHTNESS_DEFAULT;
        g_elem_state[i].state.actual[T_TAR] = LIGHTNESS_DEFAULT;
        g_elem_state[i].powerup.last_actual = LIGHTNESS_DEFAULT;
#endif
#ifdef CONFIG_MESH_MODEL_CTL_SRV
        g_elem_state[i].state.temp[T_CUR] = CTL_TEMP_DEFAULT;
        g_elem_state[i].state.temp[T_TAR] = CTL_TEMP_DEFAULT;
        g_elem_state[i].state.UV[T_CUR]   = CTL_UV_DEFAULT;
        g_elem_state[i].state.UV[T_TAR]   = CTL_UV_DEFAULT;
#endif
#ifdef CONFIG_MESH_MODEL_HSL_SRV
        g_elem_state[i].state.hue[T_CUR]        = HUE_DEFAULT;
        g_elem_state[i].state.hue[T_TAR]        = HUE_DEFAULT;
        g_elem_state[i].state.saturation[T_CUR] = SATURATION_DEFAULT;
        g_elem_state[i].state.saturation[T_TAR] = SATURATION_DEFAULT;
        g_elem_state[i].state.lightness[T_CUR]  = LIGHTNESS_DEFAULT;
        g_elem_state[i].state.lightness[T_TAR]  = LIGHTNESS_DEFAULT;
#endif
        i++;
    }

    esp_err_t err = ble_mesh_nvs_store(NVS_HANDLE, LIGHT_STATE_KEY, g_elem_state, sizeof(g_elem_state));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store %s, err: %d", LIGHT_STATE_KEY, err);
    }
}

void save_light_state(elem_state_t *p_elem)
{
    ENTER_FUNC();

    esp_err_t err = ble_mesh_nvs_store(NVS_HANDLE, LIGHT_STATE_KEY, g_elem_state, sizeof(g_elem_state));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store %s, err: %d", LIGHT_STATE_KEY, err);
    }
}

void load_light_state(void)
{
    ENTER_FUNC();
    esp_err_t ret = ESP_OK;
    bool    exist = false;

    ret = ble_mesh_nvs_restore(NVS_HANDLE, LIGHT_STATE_KEY, g_elem_state, sizeof(g_elem_state), &exist);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore light state, err: %d", ret);
        reset_light_state();
    }
}
