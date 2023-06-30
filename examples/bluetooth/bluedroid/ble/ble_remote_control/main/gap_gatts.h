#pragma once

#include <string.h> // memcpy

#include "esp_log.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"

// Example include
#include "hidd.h"

#define APP_ID_HID 0x1812
#define APP_ID_BAT_SVC 0x180F

typedef struct {
    uint16_t gatts_interface;
    uint16_t app_id;
    uint16_t conn_id;
    esp_gatts_cb_t callback;
    esp_bd_addr_t remote_bda;
} gatts_profile_inst_t;

/** 
 * Generic Access Profile - GAP Related Functions
*/

void gap_set_security(void);

void gap_event_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

/** 
 * Generic ATTribute Profile (Server) - GATTS Related Functions
*/

void gatts_event_callback(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param);

esp_err_t deinit_gap_gatts(void);
