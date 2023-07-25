/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#ifndef _BOARD_H_
#define _BOARD_H_

#include "driver/gpio.h"
#include "light_driver.h"

#ifdef __cplusplus
extern "C" {
#endif /**< __cplusplus */

#define LIGHT_STATE_KEY  "light_state"

#define LIGHT_MESH_ELEM_STATE_COUNT     1

#define CONFIG_MESH_MODEL_GEN_ONOFF_SRV 1
#define CONFIG_MESH_MODEL_GEN_LEVEL_SRV 1
#define CONFIG_MESH_MODEL_LIGHTNESS_SRV 1
#define CONFIG_MESH_MODEL_CTL_SRV       1
#define CONFIG_MESH_MODEL_HSL_SRV       1

#define GEN_ONOFF_DEFAULT   1
#define LEVEL_DEFAULT       0xE666  // 90%
#define HUE_DEFAULT         0xE666  // 90%
#define SATURATION_DEFAULT  0xE666  // 90%
#define LIGHTNESS_DEFAULT   0xE666  // 90%

#define CTL_TEMP_MIN        800
#define CTL_TEMP_MAX        20000
#define CTL_TEMP_DEFAULT    CTL_TEMP_MAX
#define CTL_UV_DEFAULT      0xE666

typedef enum {
    T_CUR = 0,
    T_TAR,
    TYPE_NUM,
} E_VALUE_TYPE;

typedef enum {
    COLOR_HSL = 0,
    COLOR_CTL = 1,
    COLOR_MAX,
} color_type_t;

typedef struct {
#ifdef CONFIG_MESH_MODEL_GEN_ONOFF_SRV
    uint8_t onoff[TYPE_NUM];
#endif

#ifdef CONFIG_MESH_MODEL_GEN_LEVEL_SRV
    int16_t level[TYPE_NUM];

    uint8_t trans_id;
    uint8_t trans_src;
    int16_t trans_level;
#endif

#ifdef CONFIG_MESH_MODEL_LIGHTNESS_SRV
    uint16_t actual[TYPE_NUM];
    uint16_t linear[TYPE_NUM];
#endif

#if defined(CONFIG_MESH_MODEL_CTL_SRV) || defined(CONFIG_MESH_MODEL_HSL_SRV)
    color_type_t color_type;
#endif

#ifdef CONFIG_MESH_MODEL_CTL_SRV
    uint16_t temp[TYPE_NUM];
    uint16_t UV[TYPE_NUM];
#endif

#ifdef CONFIG_MESH_MODEL_HSL_SRV
    uint16_t hue[TYPE_NUM];
    uint16_t saturation[TYPE_NUM];
    uint16_t lightness[TYPE_NUM];
#endif
} model_state_t;

typedef struct {
#ifdef CONFIG_MESH_MODEL_GEN_ONOFF_SRV
    uint8_t default_onoff;
    uint8_t last_onoff;
#endif

#ifdef CONFIG_MESH_MODEL_GEN_LEVEL_SRV
    int16_t default_level;
    int16_t last_level;
#endif

#ifdef CONFIG_MESH_MODEL_LIGHTNESS_SRV
    uint16_t default_actual;
    uint16_t min_actual;
    uint16_t max_actual;
    uint16_t last_actual;
#endif

#ifdef CONFIG_MESH_MODEL_CTL_SRV
    uint16_t default_temp;
    uint16_t min_temp;
    uint16_t max_temp;
    uint16_t last_temp;

    uint16_t default_UV;
    uint16_t last_UV;
#endif

#ifdef CONFIG_MESH_MODEL_HSL_SRV
    uint16_t last_hue;
    uint16_t last_saturation;
    uint16_t last_lightness;
#endif

    uint8_t range_status;
} model_powerup_t;

typedef struct {
    uint8_t elem_index;
    model_state_t state;        /**< Used to save the current state and target state of the device */
    model_powerup_t powerup;    /**< Used to save the device's power-down state and the previous state */
    void *user_data;
} elem_state_t;

typedef enum {
    /* !!!START!!! --- Don't add new ID before this one */
    BLE_MESH_APP_EVT_START = 0,

    /* Reset Related Operation, with prefix of BLE_MESH_APP_EVT_RESET_  */
    BLE_MESH_APP_EVT_RESET_SW = BLE_MESH_APP_EVT_START, /* triggered from cloud */
    BLE_MESH_APP_EVT_RESET_HW_START,                    /* triggered from user */
    BLE_MESH_APP_EVT_RESET_HW_DONE,                     /* triggered by reset by repeat module */

    /* Mesh SDK triggered event, with prefix of BLE_MESH_APP_EVT_MESH_ */
    BLE_MESH_APP_EVT_MESH_INIT,
    BLE_MESH_APP_EVT_MESH_UNPRO_ADV_START,
    BLE_MESH_APP_EVT_MESH_UNPRO_ADV_TIMEOUT,

    BLE_MESH_APP_EVT_MESH_PROV_START,
    BLE_MESH_APP_EVT_MESH_PROV_DATA,
    BLE_MESH_APP_EVT_MESH_PROV_TIMEOUT,
    BLE_MESH_APP_EVT_MESH_PROV_ALREADY,
    BLE_MESH_APP_EVT_MESH_PROV_SUCCESS,
    BLE_MESH_APP_EVT_MESH_PROV_FAIL,
    BLE_MESH_APP_EVT_MESH_PROV_RESET,

    BLE_MESH_APP_EVT_MESH_APPKEY_ADD,
    BLE_MESH_APP_EVT_MESH_APPKEY_DEL,
    BLE_MESH_APP_EVT_MESH_APPKEY_UPDATE,
    BLE_MESH_APP_EVT_MESH_NETKEY_ADD,
    BLE_MESH_APP_EVT_MESH_NETKEY_DEL,
    BLE_MESH_APP_EVT_MESH_NETKEY_UPDATE,
    BLE_MESH_APP_EVT_MESH_SUB_ADD,
    BLE_MESH_APP_EVT_MESH_SUB_DEL,

    /* State event, with prefix of BLE_MESH_APP_EVT_STATE_ */
    BLE_MESH_APP_EVT_STATE_LOAD,
    BLE_MESH_APP_EVT_STATE_UPDATE,
    BLE_MESH_APP_EVT_STATE_ACTION_DONE,

    /* Board specific event, with prefix of BLE_MESH_APP_EVT_BOARD_ */
    BLE_MESH_APP_EVT_BOARD_BUTTON_TAP,

    /* OTA specific event, with prefix of BLE_MESH_APP_EVT_OTA_ */
    BLE_MESH_APP_EVT_OTA_RECV_START = 100,
    BLE_MESH_APP_EVT_OTA_SEND_START,
    BLE_MESH_APP_EVT_OTA_RECV_SUCCESS,
    BLE_MESH_APP_EVT_OTA_SEND_SUCCESS,
    BLE_MESH_APP_EVT_OTA_RECV_FAILED,
    BLE_MESH_APP_EVT_OTA_SEND_FAILED,

    /* !!!END!!! --- Don't add new ID after this one */
    BLE_MESH_APP_EVT_END
} BLE_MESH_APP_EVENT;

/**
 * @brief
 *
 * @return esp_err_t
 */
esp_err_t board_init(void);

/**
 * @brief
 *
 * @param elem_index
 * @param hue
 * @param saturation
 * @param lightness
 */
void board_led_hsl(uint8_t elem_index, uint16_t hue, uint16_t saturation, uint16_t lightness);

/**
 * @brief
 *
 * @param elem_index
 * @param temperature
 * @param lightness
 */
void board_led_ctl(uint8_t elem_index, uint16_t temperature, uint16_t lightness);

/**
 * @brief
 *
 * @param elem_index
 * @param temperature
 */
void board_led_temperature(uint8_t elem_index, uint16_t temperature);

/**
 * @brief
 *
 * @param elem_index
 * @param actual
 */
void board_led_lightness(uint8_t elem_index, uint16_t actual);

/**
 * @brief
 *
 * @param elem_index
 * @param onoff
 */
void board_led_switch(uint8_t elem_index, uint8_t onoff);

/**
 * @brief
 *
 * @param actual
 * @return uint16_t
 */
uint16_t convert_lightness_actual_to_linear(uint16_t actual);

/**
 * @brief
 *
 * @param linear
 * @return uint16_t
 */
uint16_t convert_lightness_linear_to_actual(uint16_t linear);

/**
 * @brief
 *
 * @param temp
 * @param min
 * @param max
 * @return int16_t
 */
int16_t convert_temperature_to_level(uint16_t temp, uint16_t min, uint16_t max);

/**
 * @brief
 *
 * @param level
 * @param min
 * @param max
 * @return uint16_t
 */
uint16_t covert_level_to_temperature(int16_t level, uint16_t min, uint16_t max);

/**
 * @brief
 *
 */
void reset_light_state(void);

/**
 * @brief
 *
 * @param p_elem
 */
void save_light_state(elem_state_t *p_elem);

/**
 * @brief
 *
 */
void load_light_state(void);

#ifdef __cplusplus
}
#endif /**< __cplusplus */

#endif
