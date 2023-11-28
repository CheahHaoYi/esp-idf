#include "conn_enow.h"

#define TAG "CONN_ENOW"

static const esp_partition_t *update_partition ;
static size_t firmware_size = 0;
static uint8_t partition_sha256[32] = {0};

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *sta_netif = NULL;

/****
 * WIFI operations
*/

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    static int retry_attempt_count = 0;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA start");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_STOP:
            ESP_LOGI(TAG, "WiFi STA stopped");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to the AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Connection to the AP fail");
            if (retry_attempt_count < WIFI_RETRY_LIMIT) {
                retry_attempt_count++;
                ESP_LOGI(TAG, "Reattempt connection to the AP");
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            break;
        default:
            ESP_LOGI(TAG, "Unhandled WIFI_EVENT: 0x%06" PRIx32, event_id);
            break;
        }
        return;
    } 
    
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP received:" IPSTR, IP2STR(&event->ip_info.ip));
        retry_attempt_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

/** 
 * @brief   Register event handler for WiFi events
*/
static esp_err_t register_handler(void)
{
    s_wifi_event_group = xEventGroupCreate();
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
                                                
    return ESP_OK;
}


esp_err_t connect_wifi_ap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (sta_netif == NULL) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    register_handler();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));


    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
            .scan_method = WIFI_SCAN_METHOD,
            .sort_method = WIFI_SORT_METHOD,
            .threshold.rssi = WIFI_THRESHOLD_RSSI,
            .threshold.authmode = WIFI_THRESHOLD_AUTHMODE,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi Init done, waiting for connection to %s", CONFIG_EXAMPLE_WIFI_SSID);

    /** Waiting until either 
     * the connection is established (WIFI_CONNECTED_BIT) 
     * or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). 
     * The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, 
        to test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to AP");
        // ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
        return ESP_OK;
    } 
    
    ESP_LOGI(TAG, "Failed to connect to AP");

    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Wifi connection failed");
        return ESP_ERR_WIFI_NOT_CONNECT;
    } 
        
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
    return ESP_FAIL;
}

static void espnow_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "espnow_event_handler, base:%s, event_id:%" PRId32, base, event_id);
}

static esp_err_t espnow_data_callback(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{

    ESP_LOGI(TAG, "Receiving espnow data from mac add below, size: %d", size);
    ESP_LOG_BUFFER_HEX(TAG, src_addr, ESPNOW_ADDR_LEN);
    espnow_add_peer(src_addr, NULL);

    return ESP_OK;
}

static esp_err_t send_frame(void)
{
    espnow_ota_info_t request_ota_info = {.type = ESPNOW_OTA_TYPE_REQUEST};

    espnow_frame_head_t frame_head = {
        .retransmit_count = 3,
        .broadcast        = true,
        .magic            = esp_random(),
    };

    for (int i = 0; i < 10; i++) {
        espnow_send(ESPNOW_DATA_TYPE_DATA, ESPNOW_ADDR_BROADCAST, &request_ota_info, 1, &frame_head, portMAX_DELAY);
        ESP_LOGI(TAG, "Send espnow packet %d", i);
        DELAY(1000);
    }

    return ESP_OK;
}

esp_err_t connection_init(void)
{
    espnow_storage_init();
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, &espnow_event_handler, NULL);

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();

    esp_err_t ret = espnow_init(&espnow_config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialise ESP-NOW, error: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, espnow_data_callback);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config for data type, error: %s", esp_err_to_name(ret));
        return ret;
    }

    return send_frame();
}

/****
 * OTA Firmware operations
*/

static esp_http_client_handle_t http_server_connect(const char *ota_url)
{
    esp_http_client_config_t http_config = {
        .url = ota_url,
        .transport_type = HTTP_TRANSPORT_UNKNOWN,
    };
    esp_http_client_handle_t http_client = esp_http_client_init(&http_config);

    if (http_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        return http_client;
    }
    
    esp_err_t ret;
    int attempt_count = 0;
    do {
        ret = esp_http_client_open(http_client, 0);

        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "<%s> Connection service failed", esp_err_to_name(ret));
        }

        if (++attempt_count > MAX_HTTP_ATTEMPTS_COUNT) {
            ESP_LOGE(TAG, "Failed to connect to http server");
            break;
        }

    } while (ret != ESP_OK);

    ESP_LOGI(TAG, "Opened HTTP connection to: %s", ota_url);

    return http_client;
}

static esp_err_t write_firmware_to_partition(esp_http_client_handle_t client, size_t total_size)
{
    esp_err_t ret;
    esp_ota_handle_t ota_handle;
    uint8_t http_buffer[OTA_DATA_PAYLOAD_LEN];

    update_partition = esp_ota_get_next_update_partition(NULL);
    /**< Commence an OTA update writing to the specified partition. */
    ret = esp_ota_begin(update_partition, total_size, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "<%s> esp_ota_begin failed", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "OTA Begin, writing to partition %s", update_partition->label);

    for (ssize_t size = 0, recv_size = 0; recv_size < total_size; recv_size += size) {
        size = esp_http_client_read(client, (char *)http_buffer, OTA_DATA_PAYLOAD_LEN);
        // ESP_LOGI(TAG, "Received %d bytes", size);

        if (size <= 0) {
            ESP_LOGW(TAG, "<%s> esp_http_client_read", esp_err_to_name(ret));
            return ESP_FAIL;
        }

        /**< Write OTA update data to partition */
        ret = esp_ota_write(ota_handle, http_buffer, size);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "<%s> esp_ota_write failed", esp_err_to_name(ret));
            break;
        }
        // ESP_LOGI(TAG, "OTA write done");
    }

    if (esp_ota_end(ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "<%s> esp_ota_end failed", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA write End");
    // return if the OTA write was successful
    return ret;
}

esp_err_t ota_firmware_fetch(const char *ota_url)
{
    esp_err_t ret = ESP_OK;
    
    esp_http_client_handle_t client = http_server_connect(ota_url);

    if (client == NULL) {
        ESP_LOGI(TAG, "Failed to connect to http server");
        ret = ESP_FAIL;
        goto FETCH_FAIL;
    }

    ESP_LOGI(TAG, "Connected to http server");

    firmware_size = esp_http_client_fetch_headers(client);

    if (firmware_size <= 0) {
        uint8_t http_buffer[OTA_DATA_PAYLOAD_LEN];

        ESP_LOGW(TAG, "Please check the address of the server");
        ret = esp_http_client_read(client, (char *)http_buffer, OTA_DATA_PAYLOAD_LEN);
        ESP_ERROR_GOTO(ret < 0, FETCH_FAIL, "<%s> Read data from http stream", esp_err_to_name(ret));

        ESP_LOGW(TAG, "Recv data: %.*s", ret, http_buffer);
        goto FETCH_FAIL;
    }

    uint32_t start_time =  xTaskGetTickCount();

    ret = write_firmware_to_partition(client, firmware_size);

    uint32_t write_time = xTaskGetTickCount() - start_time;

    ESP_LOGI(TAG, "Firmware download %s, time taken: %" PRIu32 "s",
        ret == ESP_OK ? "success" : "failed", write_time * portTICK_PERIOD_MS/ 1000);

    esp_partition_get_sha256(update_partition, partition_sha256);

FETCH_FAIL:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ret;
}

esp_err_t upgrade_ota_data_cb(size_t read_offset, void *buffer_dst, size_t read_size)
{
    return esp_partition_read(update_partition, read_offset, buffer_dst, read_size);
}

espnow_addr_t* get_dest_node_list(size_t num_node, espnow_ota_responder_t *node_list)
{
    espnow_addr_t *addr_list = ESP_MALLOC(num_node * ESPNOW_ADDR_LEN);

    for (size_t i = 0; i < num_node; i++) {
        memcpy(addr_list[i], node_list[i].mac, ESPNOW_ADDR_LEN);
    }

    return addr_list;
}


esp_err_t ota_firmware_send(void)
{
    esp_err_t ret;

    espnow_ota_responder_t *info_list = NULL;
    size_t num_responder = 0;
    int scan_retry = 0;

    while (scan_retry++ < ENOW_SCAN_RETRY_LIMIT) {
        ESP_LOGI(TAG, "Scanning for nodes ......");
        espnow_ota_initiator_scan(&info_list, &num_responder, pdMS_TO_TICKS(5000));

        if (num_responder <= 1) {
            ESP_LOGW(TAG, "No nodes found, scan again in 1s");
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            break;
        }
    }
 
    if (num_responder == 0) {
        ESP_LOGW(TAG, "Still no nodes found, exiting");
        return ESP_FAIL;
    }

    espnow_addr_t* dest_addr_list = get_dest_node_list(num_responder, info_list);
    espnow_ota_initiator_scan_result_free();

    ESP_LOGI(TAG, "Sending OTA data to %d nodes", num_responder);
    
    uint32_t start_time = xTaskGetTickCount();    
    espnow_ota_result_t espnow_ota_result = {0};
    ret = espnow_ota_initiator_send(dest_addr_list, num_responder, partition_sha256, 
        firmware_size, upgrade_ota_data_cb, &espnow_ota_result);

    uint32_t send_time = xTaskGetTickCount() - start_time;

    ESP_LOGI(TAG, "Firmware distribution %s, time taken: %" PRIu32 "s",
        ret == ESP_OK ? "successful" : "failed", send_time * portTICK_PERIOD_MS/ 1000);
    ESP_LOGI(TAG, "Number of device sucessful=%d, failed=%d", 
        espnow_ota_result.successed_num, espnow_ota_result.unfinished_num);

    ESP_FREE(dest_addr_list);
    espnow_ota_initiator_result_free(&espnow_ota_result);

    return ret;
}