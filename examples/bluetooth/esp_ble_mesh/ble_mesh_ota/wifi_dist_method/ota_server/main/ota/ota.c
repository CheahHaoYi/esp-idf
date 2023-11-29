/* ota.h - OTA update utility */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ota.h"

static const char *TAG = "OTA";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static esp_err_t http_event_handler(esp_http_client_event_t *evt);

static char url_buffer[OTA_URL_SIZE] = {0};
static bool has_ota_handle_registered = false;

static uint64_t expected_ota_size;
static uint64_t ota_size_progress = 0; 

static esp_http_client_config_t http_config = {
    .event_handler = http_event_handler,
    .keep_alive_enable = false,
    .timeout_ms = OTA_RECV_TIMEOUT,
    
    #ifdef CONFIG_EXAMPLE_USE_CERT_BUNDLE
    .crt_bundle_attach = esp_crt_bundle_attach,
    #else 
    .cert_pem = (char *)server_cert_pem_start,
    #endif 

    #ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    .skip_cert_common_name_check = true,
    #endif
};

esp_err_t set_ota_url(uint8_t *url, uint16_t len)
{
    if (url == NULL) {
        ESP_LOGE(TAG, "Invalid URL");
        return ESP_ERR_INVALID_ARG;
    }

    if (len > OTA_URL_SIZE) {
        ESP_LOGE(TAG, "URL length exceeds buffer size");
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(url_buffer, (char *)url, sizeof(url_buffer));
    http_config.url = url_buffer;

    return ESP_OK;
}

esp_err_t set_expected_ota_size(uint64_t received_size)
{
    if (received_size == 0) {
        ESP_LOGE(TAG, "Invalid expected size");
        return ESP_ERR_INVALID_ARG;
    }

    expected_ota_size = received_size;
    return ESP_OK;
}

static esp_err_t http_client_init_cb(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_OK;
    /* Uncomment to add custom headers to HTTP request */
    // err = esp_http_client_set_header(http_client, "Custom-Header", "Value");
    return err;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static void ota_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base != ESP_HTTPS_OTA_EVENT) {
        ESP_LOGW(TAG, "Event base does not match OTA event handler");
        return;
    }

    switch (event_id) {
        case ESP_HTTPS_OTA_START:
            ESP_LOGI(TAG, "OTA started");
            break;
        case ESP_HTTPS_OTA_CONNECTED:
            ESP_LOGI(TAG, "Connected to server");
            break;
        case ESP_HTTPS_OTA_GET_IMG_DESC:
            ESP_LOGI(TAG, "Reading Image Description");
            break;
        case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
            ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
            break;
        case ESP_HTTPS_OTA_DECRYPT_CB:
            ESP_LOGI(TAG, "Callback to decrypt function");
            break;
        case ESP_HTTPS_OTA_WRITE_FLASH:
            ESP_LOGD(TAG, "Writing to flash: %d written", *(int *)event_data);
            break;
        case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
            ESP_LOGI(TAG, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
            break;
        case ESP_HTTPS_OTA_FINISH:
            ESP_LOGI(TAG, "OTA finish");
            break;
        case ESP_HTTPS_OTA_ABORT:
            ESP_LOGI(TAG, "OTA abort");
            break;
        default:
            ESP_LOGW(TAG, "Unknown OTA event: %04" PRIx32, event_id);
            break;
    }
}

/**
 * @brief   Send OTA size update to mesh client
 * @note    Wrapper for RTOS timer
*/
static void update_ota_progress(TimerHandle_t handle)
{   
    send_ota_size_update(ota_size_progress);
}

/**
 * @brief   Log SHA-256 hash of current firmware and bootloader
*/
static void log_partition_sha256_info(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition = {
        .address = ESP_BOOTLOADER_OFFSET,
        .size = ESP_PARTITION_TABLE_OFFSET,
        .type = ESP_PARTITION_TYPE_APP,
    };

    ESP_LOGI(TAG, "********** Partition Hash Info Start **********");
    esp_partition_get_sha256(&partition, sha_256);
    ESP_LOGI(TAG, "SHA-256 for bootloader: ");
    ESP_LOG_BUFFER_HEX(TAG, sha_256, HASH_LEN);

    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    ESP_LOGI(TAG, "SHA-256 for current firmware: ");
    ESP_LOG_BUFFER_HEX(TAG, sha_256, HASH_LEN);
    ESP_LOGI(TAG, "********** Partition Hash Info End **********");

}

/**
 * @brief   Log Info of incoming firmware update
*/
static void log_ota_app_info(esp_app_desc_t *app_desc)
{
    ESP_LOGI(TAG, "********** Incoming OTA App Info Start **********");

    ESP_LOGI(TAG, "App Name: %s", app_desc->project_name);
    ESP_LOGI(TAG, "App Version: %s", app_desc->version);
    ESP_LOGI(TAG, "App IDF Version: %s", app_desc->idf_ver);
    ESP_LOGI(TAG, "App Compile Date/Time: \t %s /\t%s", app_desc->date, app_desc->time);
     
    ESP_LOGI(TAG, "********** Incoming OTA App Info End **********");
}


/**
 * @brief   Perform firmware download over HTTPS
*/
static esp_err_t ota_firmware_download(esp_https_ota_handle_t https_ota_handle)
{
    esp_err_t err = ESP_ERR_HTTPS_OTA_IN_PROGRESS;
    // Test for OTA size
    int ota_img_size = esp_https_ota_get_image_size(https_ota_handle);

    ESP_LOGI(TAG, "OTA Image size, from OTA server: %d, from MESH client: %" PRIu64, 
        ota_img_size, expected_ota_size);

    int read_len = 0; 
    bool is_ota_in_progress = true;

    while(is_ota_in_progress) {
        err = esp_https_ota_perform(https_ota_handle);

        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS && err != ESP_OK) {
            ESP_LOGI(TAG, "ESP HTTPS OTA perform failed, err = %d", err);
            break;
        }

        read_len = esp_https_ota_get_image_len_read(https_ota_handle);

        ESP_LOGI(TAG, "Image bytes read: %d, status: %s", read_len,
            (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) ? "in progress" : "finished");

        ota_size_progress = (uint64_t)read_len; // update variable used by timer to update progress periodically

        if (ota_img_size == -1) { // terminating condition depends on if server provides content-length header             
            // use the expected ota size provided by mesh client 
            // if OTA server did not provide the content-length header
            is_ota_in_progress = (read_len < expected_ota_size);  

        } else {
            // use the ota size provided by the server if it is available
            is_ota_in_progress = (read_len < ota_img_size);
        }
    } 

    ESP_LOGI(TAG, "Total bytes read: %d", read_len);
    send_ota_size_update(read_len); // update progress one last time
    
    if (ota_img_size == -1) {
        return (read_len == expected_ota_size) ? ESP_OK : ESP_FAIL;
    }
    // if no content-length header is provided by the server, the API below will always return false
    return esp_https_ota_is_complete_data_received(https_ota_handle) ? ESP_OK : ESP_FAIL;    
}

esp_err_t ota_update(void)
{
    esp_err_t err = ESP_OK;

    log_partition_sha256_info();

    ESP_LOGI(TAG, "Starting OTA Update");

    if (!has_ota_handle_registered) {
        // singleton ota_event_handler in case of repeated calls to ota_update
        ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &ota_event_handler, NULL));
        has_ota_handle_registered = true;
    }
    
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .http_client_init_cb = http_client_init_cb,
    };
    esp_https_ota_handle_t https_ota_handle = NULL;
    err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        return err;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Get Image Description failed");
        goto ota_end;
    }

    log_ota_app_info(&app_desc);

    // timer to update progress periodically
    TimerHandle_t ota_progress_timer = xTimerCreate("OTA Progress Timer", 
            OTA_UPDATE_INTERVAL / portTICK_PERIOD_MS, pdTRUE, NULL, update_ota_progress);
    if (ota_progress_timer) {
        xTimerStart(ota_progress_timer, 0);
        ESP_LOGI(TAG, "OTA Progress Timer created");
    }
    
    err = ota_firmware_download(https_ota_handle);

    if (xTimerIsTimerActive(ota_progress_timer) != pdFALSE) {
        xTimerDelete(ota_progress_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "OTA Progress Timer deleted.");
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA firmware download failed, err = %d", err);
        goto ota_end;
    }

    err = esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL)); // needed if ota_perform does not finish properly
    err = esp_https_ota_finish(https_ota_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA upgrade successful.");
        return ESP_OK;
    } 

    // return esp_https_ota(&ota_config);
ota_end:
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed");
    return ESP_FAIL;
}
