/**
 * @file joystick_input.c
 * @brief Reading analog value from the controller, map to button direction 
 * 
 * @details User and modify the components in this file for custom commands
*/

#include "joystick_input.h"

static const char *TAG = "JOYSTICK_INPUT";

static int raw_x = 0;
static int raw_y = 0;

static uint8_t mapped_x = 0;
static uint8_t mapped_y = 0;

adc_oneshot_unit_handle_t adc_handle;
adc_cali_handle_t calibrate_handle_x;
adc_cali_handle_t calibrate_handle_y;

esp_err_t config_adc_calibration(adc_channel_t channel, adc_cali_handle_t*  cali_handle)
{
    esp_err_t ret;
    adc_cali_handle_t handle = NULL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    // ADC Calibration
    adc_cali_curve_fitting_config_t cali_config_curve = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    
    ret = adc_cali_create_scheme_curve_fitting(&cali_config_curve, &handle);
    ESP_LOGI(TAG, "ADC Calibration using curve fitting");

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    // ADC Calibration
    adc_cali_line_fitting_config_t cali_config_line = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };

    ret = adc_cali_create_scheme_line_fitting(&cali_config_line, &handle);
    ESP_LOGI(TAG, "ADC Calibration using line fitting");

#else
    ESP_LOGE(TAG, "No calibration scheme supported");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    *cali_handle = handle;
    return ret;
}

esp_err_t config_joystick_input() 
{
    // ADC Init
    
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // ADC Config
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, JOYSTICK_IN_X_ADC_CHANNEL, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, JOYSTICK_IN_Y_ADC_CHANNEL, &config));
    
    // Calibration
    esp_err_t ret;
    ret = config_adc_calibration(JOYSTICK_IN_X_ADC_CHANNEL, &calibrate_handle_x);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC Calibration failed for X axis");
        return ret;
    }
    ret = config_adc_calibration(JOYSTICK_IN_Y_ADC_CHANNEL, &calibrate_handle_y);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC Calibration failed for Y axis");
        return ret;
    }

    return ESP_OK;
}

uint8_t map_range(int x, int in_min, int in_max, int out_min, int out_max)
{
    int result = (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    return (uint8_t) result;
}

void read_joystick_input(uint8_t *x_axis, uint8_t *y_axis)
{
    // Read ADC
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, JOYSTICK_IN_X_ADC_CHANNEL, &raw_x));
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, JOYSTICK_IN_Y_ADC_CHANNEL, &raw_y));

    // ESP_LOGI(TAG, "Raw X: %d, Raw Y: %d", raw_x, raw_y);

    // Map ADC to 8-bit range
    mapped_x = map_range(raw_x, 0, ADC_RAW_MAX, 0, UINT8_MAX);
    mapped_y = map_range(raw_y, 0, ADC_RAW_MAX, 0, UINT8_MAX);

    // ESP_LOGI(TAG, "Mapped X: %d, Mapped Y: %d", mapped_x, mapped_y);
    
    // Assign value
    *x_axis = mapped_x;
    *y_axis = mapped_y;
}

void deinit_adc_calibration(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#else
    ESP_LOGE(TAG, "No calibration scheme supported");
#endif
}

esp_err_t deinit_joystick_input() 
{
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
    deinit_adc_calibration(calibrate_handle_x);
    deinit_adc_calibration(calibrate_handle_y);
    return ESP_OK;
}

esp_err_t config_button_input()
{   
    ESP_ERROR_CHECK(gpio_set_direction(PIN_BUTTON_A, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(PIN_BUTTON_B, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(PIN_BUTTON_C, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(PIN_BUTTON_D, GPIO_MODE_INPUT));
    return ESP_OK;
}

// Return an 8 bit value
// Least significant 4 bits represent buttons A, B, C, D
void read_button_input(uint8_t *button_in) 
{
    uint8_t button_input = 0;
    button_input |= gpio_get_level(PIN_BUTTON_A) << 0;
    button_input |= gpio_get_level(PIN_BUTTON_B) << 1;
    button_input |= gpio_get_level(PIN_BUTTON_C) << 2;
    button_input |= gpio_get_level(PIN_BUTTON_D) << 3;

    *button_in = button_input;
}

esp_err_t deinit_button_input()
{
    ESP_ERROR_CHECK(gpio_reset_pin(PIN_BUTTON_A));
    ESP_ERROR_CHECK(gpio_reset_pin(PIN_BUTTON_B));
    ESP_ERROR_CHECK(gpio_reset_pin(PIN_BUTTON_C));
    ESP_ERROR_CHECK(gpio_reset_pin(PIN_BUTTON_D));
    return ESP_OK;
}