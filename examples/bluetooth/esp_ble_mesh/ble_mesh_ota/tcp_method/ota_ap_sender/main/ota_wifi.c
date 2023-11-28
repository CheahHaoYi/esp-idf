#include "ota_wifi.h"

#define TAG "wifi_softap_ota"

static EventGroupHandle_t wifi_event_group;
static esp_netif_t* netif_sta;
static esp_netif_t* netif_ap;

static int s_retry_num = 0;

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGI(TAG, "lost ip");
            break;
        case IP_EVENT_AP_STAIPASSIGNED:
            ESP_LOGI(TAG, "assigned ip");
            break;
        default:
            ESP_LOGI(TAG, "unhandled ip_event, id %" PRId32, event_id);
            break;
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    
    switch (event_id) {
        case WIFI_EVENT_AP_STACONNECTED: 
            wifi_event_ap_staconnected_t* conn_event = (wifi_event_ap_staconnected_t*) event_data;
            ESP_LOGI(TAG, "station "MACSTR" connected to AP, AID=%d",
                 MAC2STR(conn_event->mac), conn_event->aid);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            wifi_event_ap_stadisconnected_t* disconn_event = (wifi_event_ap_stadisconnected_t*) event_data;
            ESP_LOGI(TAG, "station "MACSTR" disconnected from AP, AID=%d",
                 MAC2STR(disconn_event->mac), disconn_event->aid);
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "wifi soft-AP started");
            break;
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "wifi station started");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "wifi station disconnected");
            if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
            } else {
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            }
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "wifi station connected");
            break;
        default:
            ESP_LOGI(TAG, "unhandled wifi_event, id %" PRId32, event_id);
            break;
    }
}


// wifi ap to connect to AP to download firmware? 
esp_err_t wifi_init_sta(void) {
    netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_STA_SSID,
            .password = EXAMPLE_ESP_WIFI_STA_PASSWD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };

    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

esp_err_t wifi_init_softap(void) {
    netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                    .required = false,
            },
        },
    };

    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_LOGI(TAG, "SoftAP Init Done, \n\tSSID:\t%s \n\tPW:\t%s \n\tchnl:\t%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);

    return ESP_OK;
}

esp_err_t wifi_wait_connection(void) 
{
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, 
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, 
                                            pdFALSE, 
                                            pdFALSE, 
                                            portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to AP");
        return ESP_FAIL;
    } 

    ESP_LOGE(TAG, "UNEXPECTED EVENT");
    return ESP_FAIL;
}

esp_err_t wifi_init() 
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &ip_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // init softap 
    ESP_ERROR_CHECK(wifi_init_softap());

    // init sta
    ESP_ERROR_CHECK(wifi_init_sta());

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wifi Started, waiting for STA connection");
    esp_err_t ret = wifi_wait_connection();

    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_netif_set_default_netif(netif_sta);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA as default netif, err: 0x%x", ret);
    }

    ret = esp_netif_napt_enable(netif_ap); 
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NAPT not enabled on the netif, err: 0x%x", ret);
    }

    esp_netif_t *netif = esp_netif_get_default_netif();
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);
    ESP_LOGI(TAG, "IP Address: "IPSTR, IP2STR(&ip_info.ip));

    // ret = esp_netif_set_default_netif(netif_ap);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to set AP as default netif, err: 0x%x", ret);
    // }

    // netif = esp_netif_get_default_netif();
    // esp_netif_get_ip_info(netif, &ip_info);
    // ESP_LOGI(TAG, "IP Address: "IPSTR, IP2STR(&ip_info.ip));

    return ESP_OK;
}