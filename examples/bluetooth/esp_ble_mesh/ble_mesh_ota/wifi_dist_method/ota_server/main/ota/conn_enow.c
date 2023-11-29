#include "conn_enow.h"

#define TAG "CONNECTION-ENOW"

SemaphoreHandle_t xSemaphore_update = NULL;
uint8_t initiator_mac_addr[ESPNOW_ADDR_LEN] = {0};

static void espnow_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "espnow_event_handler, base:%s, event_id:%" PRId32, base, event_id);

    switch (event_id) {
        case (ESP_EVENT_ESPNOW_OTA_STARTED):
            ESP_LOGI(TAG, "OTA started");
            xSemaphoreGive(xSemaphore_update); 
            break;
        case (ESP_EVENT_ESPNOW_OTA_STATUS):
            espnow_ota_status_t ota_status = {0};
            espnow_ota_responder_get_status(&ota_status);
            ESP_LOGI(TAG, "OTA status: total size-%" PRIu32 ", written size-%" PRIu32 ", err code: %x",
                     ota_status.total_size, ota_status.written_size, ota_status.error_code);
            break;
        case (ESP_EVENT_ESPNOW_OTA_FINISH):
            ESP_LOGI(TAG, "OTA finished");
            // exit ota update task
            xSemaphoreGive(xSemaphore_update); 
            break;
        default:
            ESP_LOGI(TAG, "Unknown event id: 0x%" PRIx32, event_id);
            break;
    }
}

static esp_err_t espnow_ota_responder_status_process(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_LOGI(TAG, "Receive Handler -> size: %d\n", size);
    ESP_LOG_BUFFER_HEX(TAG, src_addr, ESPNOW_ADDR_LEN);
    memcpy(initiator_mac_addr, src_addr, ESPNOW_ADDR_LEN);
    espnow_add_peer(initiator_mac_addr, NULL);
  
    espnow_ota_info_t request_ota_info = {.type = ESPNOW_OTA_TYPE_REQUEST};

    espnow_frame_head_t frame_head = {
        .retransmit_count = 3,
        .broadcast        = true,
        // .magic            = esp_random(),
    };
    
    espnow_send(ESPNOW_DATA_TYPE_DATA, initiator_mac_addr, &request_ota_info, 1, &frame_head, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t enow_init(void)
{
    ESP_ERROR_CHECK(espnow_storage_init());

    ESP_ERROR_CHECK(esp_netif_init());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start());
    ESP_ERROR_CHECK( esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, &espnow_event_handler, NULL));

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(espnow_init(&espnow_config));

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, espnow_ota_responder_status_process);

    espnow_ota_config_t ota_config = {
        .skip_version_check = true,
        .progress_report_interval = 10, /** Percentage interval to save OTA status and report ota status event */
    };

    ESP_ERROR_CHECK(espnow_ota_responder_start(&ota_config));
    ESP_LOGI(TAG, "ESPNOW OTA responder init");

    return ESP_OK;
}

esp_err_t enow_update(void)
{    
    xSemaphore_update = xSemaphoreCreateBinary();
    uint32_t start_time = xTaskGetTickCount();

    esp_err_t ret = ESP_FAIL;
    int wait_count = 0;

    while(1) {
        // Wait for ESP NOW connection
        if (xSemaphoreTake(xSemaphore_update, WAIT_PERIOD) == pdTRUE) {
            ESP_LOGI(TAG, "ESP_NOW started");
            break;
        }

        if (wait_count++ > MAX_WAIT_COUNT) {
            ESP_LOGI(TAG, "ESP_NOW connection timeout");
            goto OTA_END;
        }
        ESP_LOGI(TAG, "Waiting for ESP NOW connection");
    }

    wait_count = 0; // reset timeout counter
    while(1) {
        // Wait for OTA to finish
        if (xSemaphoreTake(xSemaphore_update, WAIT_PERIOD) == pdTRUE) {
            ESP_LOGI(TAG, "OTA finished");
            ret = ESP_OK;
            break;
        }

        if (wait_count++ > MAX_WAIT_COUNT) {
            ESP_LOGI(TAG, "OTA timeout");
            break;
        }
    }

OTA_END:
    uint32_t done_time = xTaskGetTickCount();
    ESP_LOGI(TAG, "Time taken for OTA: %" PRIu32 " seconds", 
        (done_time - start_time) * portTICK_PERIOD_MS / 1000);
    espnow_ota_responder_stop();
    return ret;
}