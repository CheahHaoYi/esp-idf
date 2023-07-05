/**
 * @file joystick_input.c
 * @brief Reading analog value from the controller, map to button direction 
 * 
 * @details User and modify the components in this file for custom commands
*/

#include "joystick_input.h"

static const char *TAG = "JOYSTICK_INPUT";

#ifdef CONFIG_JOYSTICK_INPUT_MODE_ADC
static int raw_x = 0;
static int raw_y = 0;
#endif

static uint8_t mapped_x = 0;
static uint8_t mapped_y = 0;

adc_oneshot_unit_handle_t adc_handle;
adc_cali_handle_t calibrate_handle_x;
adc_cali_handle_t calibrate_handle_y;

esp_err_t config_adc_calibration(adc_channel_t channel, adc_cali_handle_t*  cali_handle); 
void deinit_adc_calibration(adc_cali_handle_t handle);
void set_button_bit(int pin_num, int pin_level);

static volatile int64_t prev_trigger_time = 0;
static uint8_t button_input = 0;

extern QueueHandle_t input_queue;

static void IRAM_ATTR button_isr_handler(void* arg)
{
    uint8_t button_pin_num = (uint8_t) arg;
    uint64_t current_time = esp_timer_get_time(); 
    
    // set bit whenever a button is pressed
    // do not send to queue yet in case of other button presses or multiple presses
    set_button_bit(button_pin_num, 1);

    // Simple button debouncing
    if (current_time - prev_trigger_time > DEBOUNCE_TIME_US) {
        input_event_t button_event = {
            .input_source = INPUT_SOURCE_BUTTON,
            .data_button = button_input,
        };

        prev_trigger_time = current_time;
        xQueueSendFromISR(input_queue, &button_event, NULL);
        button_input = 0; 
    }
}

void set_button_bit(int pin_num, int pin_level)
{
    switch (pin_num) {
    case PIN_BUTTON_BOOT: // fallthrough
    case PIN_BUTTON_A:
        button_input |= (pin_level << 0);
        break;
    case PIN_BUTTON_B:
        button_input |= (pin_level << 1);
        break;
    case PIN_BUTTON_C:
        button_input |= (pin_level << 2);
        break;
    case PIN_BUTTON_D:  
        button_input |= (pin_level << 3);
        break;
    default:    
        break;
    }
}

uint8_t map_range(int x, int in_min, int in_max, int out_min, int out_max)
{
    int result = (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    return (uint8_t) result;
}

esp_err_t config_joystick_input(void) 
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

    // ADC Calibration
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

esp_err_t deinit_joystick_input(void) 
{
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
    deinit_adc_calibration(calibrate_handle_x);
    deinit_adc_calibration(calibrate_handle_y);
    return ESP_OK;
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

void print_console_read_help(void)
{
    ESP_LOGI(TAG, "Console input help:");
    ESP_LOGI(TAG, "\t Top Left(q) \t\t Top Center(w) \t\t\t Top Right(e)");
    ESP_LOGI(TAG, "\t Middle Left(a) \t Middle Center(s) \t Middle Right(d)");
    ESP_LOGI(TAG, "\t Bottom Left(z) \t Bottom Center(x) \t Bottom Right(c)");
    ESP_LOGI(TAG, "Press Enter to send input, make sure caps lock is off\n");
}

void console_read_joystick_input(void *args)
{
    while (true) {
        DELAY(HID_LATENCY);

        // Read from console, to emulate joystick input on PC
        int joystick_input = fgetc(stdin);

        // Skip invalid value
        if (joystick_input == EOF || isspace(joystick_input)) {
            DELAY(100);
            continue;
        }

        input_event_t joystick_event = {
            .input_source = INPUT_SOURCE_CONSOLE,
            .data_console = (uint8_t) joystick_input,
        };
        xQueueSendFromISR(input_queue, &joystick_event, NULL);
    }
}

void char_to_joystick_input(uint8_t user_input, uint8_t *x_axis, uint8_t *y_axis) 
{
    switch (user_input) {
    case 'q':
        *x_axis = 0;
        *y_axis = 0;
        break;
    case 'w':
        *x_axis = UINT8_MAX / 2;
        *y_axis = 0;
        break;
    case 'e':
        *x_axis = UINT8_MAX;
        *y_axis = 0;
        break;
    case 'a':
        *x_axis = 0;
        *y_axis = UINT8_MAX / 2;
        break;
    case 's':
        *x_axis = UINT8_MAX / 2;
        *y_axis = UINT8_MAX / 2;
        break;  
    case 'd':   
        *x_axis = UINT8_MAX;
        *y_axis = UINT8_MAX / 2;
        break;
    case 'z':
        *x_axis = 0;
        *y_axis = UINT8_MAX;
        break;
    case 'x':
        *x_axis = UINT8_MAX / 2;
        *y_axis = UINT8_MAX;
        break;
    case 'c':
        *x_axis = UINT8_MAX;
        *y_axis = UINT8_MAX;
        break;
    default:
        ESP_LOGI(TAG, "Invalid input");
        break;
    }
}

void read_joystick_input(uint8_t *x_axis, uint8_t *y_axis)
{
#ifdef CONFIG_JOYSTICK_INPUT_MODE_ADC
    // Read ADC
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, JOYSTICK_IN_X_ADC_CHANNEL, &raw_x));
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, JOYSTICK_IN_Y_ADC_CHANNEL, &raw_y));

    // Map ADC to 8-bit range
    mapped_x = map_range(raw_x, 0, ADC_RAW_MAX, 0, UINT8_MAX);
    mapped_y = map_range(raw_y, 0, ADC_RAW_MAX, 0, UINT8_MAX);
#else 
    // Return default value if not using ADC
    mapped_x = UINT8_MAX / 2;
    mapped_y = UINT8_MAX / 2;
#endif
    // Assign value
    *x_axis = mapped_x;
    *y_axis = mapped_y;
}

void ext_hardware_joystick(void *args)
{   
    uint8_t curr_x = 0, curr_y = 0, prev_x = 0, prev_y = 0;

    while (true) {
        DELAY(HID_LATENCY);
        read_joystick_input(&curr_x, &curr_y);
        
        // Skip if value is within threshold
        if (abs(curr_x - prev_x) < JOYSTICK_THRESHOLD && abs(curr_y - prev_y) < JOYSTICK_THRESHOLD) {
            DELAY(100);
            continue;
        }
        // Update previous value
        prev_x = curr_x;
        prev_y = curr_y;

        // Send to input queue
        input_event_t joystick_event = {
            .input_source = INPUT_SOURCE_JOYSTICK,
            .data_joystick_x = curr_x,
            .data_joystick_y = curr_y,
        };

        xQueueSendFromISR(input_queue, &joystick_event, NULL);
    }
}

esp_err_t config_button_input(void)
{   
    const gpio_config_t pin_config = {
        .pin_bit_mask = BUTTON_PIN_BIT_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };

    ESP_ERROR_CHECK(gpio_config(&pin_config));

    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1));

#ifdef CONFIG_BUTTON_INPUT_MODE_BOOT
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BUTTON_BOOT, button_isr_handler, (void*) PIN_BUTTON_BOOT));
#else // CONFIG_BUTTON_INPUT_MODE_GPIO
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BUTTON_A, button_isr_handler, (void*) PIN_BUTTON_A));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BUTTON_B, button_isr_handler, (void*) PIN_BUTTON_B));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BUTTON_C, button_isr_handler, (void*) PIN_BUTTON_C));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BUTTON_D, button_isr_handler, (void*) PIN_BUTTON_D));
#endif

    esp_timer_early_init();
    return ESP_OK;
}

// Return an 8 bit value
// Least significant 4 bits represent buttons A, B, C, D
void read_button_input(uint8_t *button_in) 
{
#ifdef CONFIG_BUTTON_INPUT_MODE_BOOT
    // Boot button is active low (high when released, low when pressed)
    set_button_bit(PIN_BUTTON_BOOT, 1 - gpio_get_level(PIN_BUTTON_BOOT));
#else // CONFIG_BUTTON_INPUT_MODE_GPIO
    set_button_bit(PIN_BUTTON_A, gpio_get_level(PIN_BUTTON_A));
    set_button_bit(PIN_BUTTON_B, gpio_get_level(PIN_BUTTON_B));
    set_button_bit(PIN_BUTTON_C, gpio_get_level(PIN_BUTTON_C));
    set_button_bit(PIN_BUTTON_D, gpio_get_level(PIN_BUTTON_D));
#endif
    // ESP_LOGI(TAG, "Button Values, A: %d, B: %d, C: %d, D: %d", 
    //     gpio_get_level(PIN_BUTTON_A), gpio_get_level(PIN_BUTTON_B), 
    //     gpio_get_level(PIN_BUTTON_C), gpio_get_level(PIN_BUTTON_D));

    *button_in = button_input;
    button_input = 0;
}

esp_err_t deinit_button_input(void)
{   
    gpio_uninstall_isr_service();
#ifdef CONFIG_BUTTON_INPUT_MODE_BOOT
    ESP_ERROR_CHECK(gpio_reset_pin(PIN_BUTTON_BOOT));
#else // CONFIG_BUTTON_INPUT_MODE_GPIO
    ESP_ERROR_CHECK(gpio_reset_pin(PIN_BUTTON_A));
    ESP_ERROR_CHECK(gpio_reset_pin(PIN_BUTTON_B));
    ESP_ERROR_CHECK(gpio_reset_pin(PIN_BUTTON_C));
    ESP_ERROR_CHECK(gpio_reset_pin(PIN_BUTTON_D));
#endif
    return ESP_OK;
}

