// Copyright 2020-2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_mesh_internal.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_ble_mesh_common_api.h"

#include "ble_mesh_ota_op.h"
#include "ble_mesh_ota_bearer.h"
#include "ble_mesh_ota_utility.h"

#define TAG "ota_ber"

#define BEARER_STA_CONN_TIMEOUT     (60 * 1000 * 1000) /* 60s */
#define BEARER_STA_UPGRADE_TIMEOUT  (120 * 1000 * 1000) /* 120s */

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* WiFi and TCP roles of ESP32: Station <---> TCP Client, SoftAP <---> TCP Server */
static struct ota_tcp_ctx_t {
    struct {
        uint8_t bssid[6];   /* bssid of the SoftAP connected by the station */
        ip4_addr_t ip;      /* ip address assigned for the station */
        ip4_addr_t netmask; /* network mask */
        ip4_addr_t gw;      /* gateway */
        int sockfd;         /* socket created by the client */
    } client;
    struct {
        uint8_t max_client; /* maximum number of TCP clients */
        struct tcp_client {
            uint8_t mac[6]; /* mac address of the station */
            bool occupied;  /* Indicate if the structure is used by a client */
            int  sockfd;    /* socket of the client which is accepted */
            struct sockaddr_in saddr; /* ip, port etc. */
        } clients[BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM];
        int sockfd; /* socket created by the server */
    } server;
} g_tcp_ctx;

static ble_mesh_ota_wifi_init_t g_wifi_init = {0};

static uint8_t g_sta_conn_count               = 0;
static esp_timer_handle_t g_sta_conn_timer    = NULL;
static esp_timer_handle_t g_sta_upgrade_timer = NULL;

static QueueHandle_t g_wifi_queue = NULL;
static QueueHandle_t g_tcp_queue  = NULL;

static esp_err_t sta_store_softap(const uint8_t bssid[6], bool *new)
{
    ENTER_FUNC();
    if (!bssid || !new) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    if (!memcmp(g_tcp_ctx.client.bssid, bssid, 6)) {
        ESP_LOGW(TAG, "Reconnected with SoftAP "MACSTR"", MAC2STR(bssid));
        if (g_tcp_ctx.client.sockfd != -1) {
            ESP_LOGW(TAG, "TCP client close socket %d", g_tcp_ctx.client.sockfd);
            close(g_tcp_ctx.client.sockfd);
            g_tcp_ctx.client.sockfd = -1;
        }
        *new = false;
        return ESP_OK;
    }

    memcpy(g_tcp_ctx.client.bssid, bssid, 6);
    *new = true;

    return ESP_OK;
}

static esp_err_t softap_store_sta(const uint8_t mac[6], bool *new)
{
    ENTER_FUNC();
    int i = 0;

    if (!mac || !new) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    for (i = 0; i < g_tcp_ctx.server.max_client; i++) {
        struct tcp_client *client = &g_tcp_ctx.server.clients[i];
        if (client->occupied && !memcmp(client->mac, mac, sizeof(client->mac))) {
            ESP_LOGW(TAG, "Reconnected station "MACSTR"", MAC2STR(mac));
            if (client->sockfd != -1) {
                ESP_LOGW(TAG, "TCP server close socket %d", client->sockfd);
                close(client->sockfd);
                client->sockfd = -1;
            }
            *new = false;
            return ESP_OK;
        }
    }

    for (i = 0; i < g_tcp_ctx.server.max_client; i++) {
        struct tcp_client *client = &g_tcp_ctx.server.clients[i];
        if (client->occupied == false) {
            memcpy(client->mac, mac, sizeof(client->mac));
            client->occupied = true;
            *new = true;
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

static esp_err_t tcp_server_store_sockfd(const uint8_t *mac, struct sockaddr_in *sock_addr, int sockfd, bool *new)
{
    ENTER_FUNC();
    if (!mac || !sock_addr || sockfd < 0 || !new) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < g_tcp_ctx.server.max_client; i++) {
        struct tcp_client *client = &g_tcp_ctx.server.clients[i];
        /* new client connect() sockinfo or client re-connect() sockinfo */
        if (client->occupied && !memcmp(client->mac, mac, sizeof(client->mac))) {
            if (client->sockfd == -1) {
                *new = true;
            } else {
                *new = false;
            }
            client->sockfd = sockfd;
            memcpy(&client->saddr, sock_addr, sizeof(struct sockaddr_in));
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

static esp_err_t tcp_client_store_sockfd(int sockfd, bool *new)
{
    ENTER_FUNC();
    if (sockfd < 0 || !new) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_tcp_ctx.client.sockfd == -1) {
        *new = true;
    } else {
        *new = false;
    }
    g_tcp_ctx.client.sockfd = sockfd;

    return ESP_OK;
}

static int get_client_socket(const uint8_t *mac)
{
    ENTER_FUNC();
    for (int i = 0; i < g_tcp_ctx.server.max_client; i++) {
        struct tcp_client *client = &g_tcp_ctx.server.clients[i];
        /* new client connect() sockinfo or client re-connect() sockinfo */
        if (client->occupied && !memcmp(client->mac, mac, sizeof(client->mac))) {
            return client->sockfd;
        }
    }

    return -1;
}

static void tcp_error(const char *str, int sockfd)
{
    ENTER_FUNC();
    int       error  = 0;
    socklen_t optlen = sizeof(int);

    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &optlen) < 0) {
        ESP_LOGE(TAG, "Get socket opt failed, %s", str);
        return;
    }

    ESP_LOGW(TAG, "%s fail, error %d, reason %s", str, error, strerror(error));
}

static esp_err_t tcp_server_bind(void)
{
    ENTER_FUNC();
    int sockfd, ret = 0;
    struct sockaddr_in sock_info = {0};

recreate:
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        tcp_error("TCP server socket()", sockfd);
        goto recreate;
    }

    sock_info.sin_family = AF_INET;
    sock_info.sin_addr.s_addr = inet_addr(BLE_MESH_OTA_IP_ADDR);
    sock_info.sin_port = htons(BLE_MESH_OTA_TCP_PORT);
    ret = bind(sockfd, (struct sockaddr *)&sock_info, sizeof(struct sockaddr));
    if (ret < 0) {
        tcp_error("TCP server bind()", sockfd);
        close(sockfd);
        goto recreate;
    }

    ret = listen(sockfd, g_wifi_init.max_conn);
    if (ret < 0) {
        tcp_error("TCP server listen()", sockfd);
        close(sockfd);
        goto recreate;
    }

    g_tcp_ctx.server.sockfd = sockfd;

    return ESP_OK;
}

static esp_err_t tcp_trigger_ota_start(const uint8_t addr[6])
{
    ENTER_FUNC();
    ble_mesh_ota_msg_t msg = {0};

    if (!addr) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    /**
     * @brief If this device OTA Done. Store other device that need ota.
     */
    if (g_wifi_init.ota_done) {
        ESP_LOGD(TAG, "recv a need ota device, %d", __LINE__);
        return ble_mesh_store_recv_ota_seg_device(addr);
    }

    /**
     * @brief If this device need OTA.
     */
    msg.flag = BLE_MESH_OTA_SEG_RECV;
    memcpy(msg.addr, addr, BLE_MESH_OTA_DEV_ADDR_LEN);

    return ble_mesh_ota_wifi_task_post(&msg, portMAX_DELAY, false);
}

static esp_err_t tcp_client_connect(const uint8_t *addr, bool *new_dev)
{
    ENTER_FUNC();
    esp_err_t err                = ESP_OK;
    int sockfd, ret              = 0;
    struct sockaddr_in sock_info = {0};

reconnect:
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        tcp_error("TCP client socket()", sockfd);
        goto reconnect;
    }

    sock_info.sin_family      = AF_INET;
    sock_info.sin_addr.s_addr = inet_addr(BLE_MESH_OTA_IP_ADDR);
    sock_info.sin_port        = htons(BLE_MESH_OTA_TCP_PORT);
    ret = connect(sockfd, (struct sockaddr *)&sock_info, sizeof(struct sockaddr));
    if (ret < 0) {
        tcp_error("TCP client connect()", sockfd);
        close(sockfd);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "local sockfd %d", sockfd);

    err = tcp_client_store_sockfd(sockfd, new_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store server socket");
        close(sockfd);
        return ESP_FAIL;
    }

    if (*new_dev) {
        return tcp_trigger_ota_start(addr);
    }

    return ESP_OK;
}

static const uint8_t *find_mac_based_on_ip(uint32_t ip_addr)
{
    ENTER_FUNC();
    esp_netif_sta_list_t sta_ip_list = {0};
    wifi_sta_list_t sta_list = {
        .num = 1,
    };
    ip4_addr_t print = {
        .addr = ip_addr,
    };

    for (int i = 0; i < g_tcp_ctx.server.max_client; i++) {
        struct tcp_client *client = &g_tcp_ctx.server.clients[i];
        if (client->occupied == true) {
            memcpy(sta_list.sta[0].mac, client->mac, BLE_MESH_OTA_DEV_ADDR_LEN);
            esp_netif_get_sta_list(&sta_list, &sta_ip_list);
            if (sta_ip_list.sta[0].ip.addr == ip_addr) {
                ESP_LOGW(TAG, "ip %s, mac "MACSTR"", inet_ntoa(print), MAC2STR(client->mac));
                return client->mac;
            }
        }
    }

    return NULL;
}

static esp_err_t tcp_server_accept(bool *new_dev)
{
    ENTER_FUNC();
    socklen_t addr_len = sizeof(struct sockaddr);
    struct sockaddr_in remote = {0};
    const     uint8_t *mac = NULL;
    int       sockfd       = -1;
    esp_err_t err          = ESP_OK;

    /* Note: the orders of SYSTEM_EVENT_AP_STACONNECTED and socket accepted may be not the same.
     * Given an example from a test result:
     * SYSTEM_EVENT_AP_STACONNECTED: Station 30:ae:a4:80:07:a8
     * SYSTEM_EVENT_AP_STACONNECTED: Station 30:ae:a4:80:1c:2c
     * SYSTEM_EVENT_AP_STACONNECTED: Station 30:ae:a4:80:19:24
     * tcp_server_accept: sockfd 56, addr 192.168.4.2, port 52687
     * find_mac_based_on_ip: ip 192.168.4.2, mac 30:ae:a4:80:07:a8
     * tcp_server_accept: sockfd 57, addr 192.168.4.4, port 60968
     * find_mac_based_on_ip: ip 192.168.4.4, mac 30:ae:a4:80:19:24
     * Socket of Station 30:ae:a4:80:1c:2c accepted takes about 464s after sockfd 57.
     */

reconnect:
    sockfd = accept(g_tcp_ctx.server.sockfd, (struct sockaddr*)&remote, &addr_len);
    if (sockfd < 0) {
        tcp_error("TCP server accept()", g_tcp_ctx.server.sockfd);
        goto reconnect;
    }

    ESP_LOGI(TAG, "accept sockfd %d, addr %s, port %d", sockfd, inet_ntoa(remote.sin_addr), htons(remote.sin_port));

    mac = find_mac_based_on_ip(remote.sin_addr.s_addr);
    if (!mac) {
        ESP_LOGE(TAG, "Failed to find related mac address");
        close(sockfd);
        goto reconnect;
    }

    err = tcp_server_store_sockfd(mac, &remote, sockfd, new_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store client socket");
        close(sockfd);
        goto reconnect;
    }

    if (*new_dev) {
        return tcp_trigger_ota_start(mac);
    }

    return ESP_OK;
}

static int get_tcp_socket(const uint8_t addr[6])
{
    ENTER_FUNC();
    if (!addr) {
        ESP_LOGE(TAG, "Invalid argument");
        return -1;
    }

    if (g_wifi_init.wifi_role == BLE_MESH_OTA_WIFI_STA && g_wifi_init.ota_done == false) {
        if (!memcmp(g_tcp_ctx.client.bssid, addr, sizeof(g_tcp_ctx.client.bssid))) {
            return g_tcp_ctx.client.sockfd;
        }
    } else {
        for (int i = 0; i < g_tcp_ctx.server.max_client; i++) {
            struct tcp_client *client = &g_tcp_ctx.server.clients[i];
            if (client->occupied && !memcmp(client->mac, addr, sizeof(client->mac))) {
                return client->sockfd;
            }
        }
    }

    ESP_LOGE(TAG, "Failed to get socket, addr "MACSTR"", MAC2STR(addr));

    return -1;
}

static esp_err_t tcp_send(ble_mesh_ota_tx_ctx_t *tx)
{
    ENTER_FUNC();
    int sockfd, sent_len, already_sent = 0;

    if (!tx) {
        ESP_LOGE(TAG, "Invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    sockfd = get_tcp_socket(tx->addr);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "Failed to get socket");
        return ESP_FAIL;
    }

    while (already_sent < tx->length) {
        sent_len = send(sockfd, tx->data + already_sent, tx->length - already_sent, 0);
        if (sent_len <= 0) {
            tcp_error("TCP send()", sockfd);
            close(sockfd);
            return ESP_FAIL;
        }

        already_sent += sent_len;
    }

    return ESP_OK;
}

static void sta_upgrade_timeout_cb(void *arg)
{
    ENTER_FUNC();

    /* May disable Wi-Fi functionality, and back to normal BLE Mesh model. */
    ESP_LOGW(TAG, "Upgrade Other device timeout");

    ble_mesh_ota_msg_t msg = {0};
    msg.flag = BLE_MESH_OTA_FAIL;
    ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);

    esp_restart();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    ENTER_FUNC();
    ble_mesh_ota_msg_t msg                   = {0};
    static             bool softap_start     = false;
    static             uint8_t retry_counter = 0;
    esp_err_t          err                   = ESP_OK;

    switch(event_id) {
    case WIFI_EVENT_SCAN_DONE:
        ESP_LOGD(TAG, "WIFI_EVENT_SCAN_DONE");
        break;
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_STOP:
        ESP_LOGD(TAG, "WIFI_EVENT_STA_STOP");
        break;
    case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
        bool new_softap = false;
        ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
        ESP_LOGD(TAG, "SoftAP bssid: "MACSTR"", MAC2STR(event->bssid));
        if (g_wifi_init.wifi_role != BLE_MESH_OTA_WIFI_STA_FROM_URL) {
            err = sta_store_softap(event->bssid, &new_softap);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store SoftAP info");
            } else {
                ESP_LOGD(TAG, "%s SoftAP", new_softap ? "New" : "Old");
            }
        }
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED, reason: %d", event->reason);
        if (g_wifi_init.ota_done == false && retry_counter++ < BLE_MESH_OTA_WIFI_MAX_RETRY_CNT) {
            /* If station is disconnected when OTA is not completed, we need to re-connect.
             * As a board whose role is station, currently it can only be a need-OTA device.
             */
            ESP_LOGI(TAG, "Reconnect ...");
            esp_wifi_connect();
        } else if (retry_counter++ >= BLE_MESH_OTA_WIFI_MAX_RETRY_CNT) {
            ble_mesh_ota_msg_t msg = {0};
            msg.flag = BLE_MESH_OTA_FAIL;
            ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);
        }
        break;
    }
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "WIFI_EVENT_AP_START, %d", __LINE__);
        if (softap_start == false) {
            ESP_LOGD(TAG, "tcp_server_bind, %d", __LINE__);
            /* Use flag here because when station is enabled and tries to start SoftAP,
             * the events come as following:
             * I (23821) OTA_WIFI: SYSTEM_EVENT_AP_START
             * W (23821) OTA_WIFI: tcp_server_bind: ip 192.168.4.1, port 8090
             * I (23841) OTA_WIFI: SYSTEM_EVENT_AP_STOP
             * I (23841) OTA_WIFI: SYSTEM_EVENT_AP_START
             */
            if ((tcp_server_bind()) != ESP_OK) {
                ESP_LOGE(TAG, "TCP server failed to bind");
                break;
            }
            softap_start = true;
        }
        break;
    case WIFI_EVENT_AP_STOP:
        ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");
        break;
    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        bool new_sta = false;
        ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");
        ESP_LOGD(TAG, "Station mac: "MACSTR"", MAC2STR(event->mac));
        ESP_LOGD(TAG, "Station aid: %d", event->aid);
        err = softap_store_sta(event->mac, &new_sta);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store station info");
            break;
        }
        if (new_sta) {
            /* Station is connected to SoftAP for the first time. */
            if (g_wifi_init.ota_done) {
                esp_timer_stop(g_sta_conn_timer);
                g_sta_conn_count++; /* "++" when each station is connected */
                if (g_sta_upgrade_timer == NULL) {
                    esp_timer_create_args_t args = {
                        .callback = &sta_upgrade_timeout_cb,
                        .name     = "sta upgrade timer",
                    };
                    ESP_LOGI(TAG, "sta upgrade timer start, %d", __LINE__);
                    ESP_ERROR_CHECK(esp_timer_create(&args, &g_sta_upgrade_timer));
                    ESP_ERROR_CHECK(esp_timer_start_once(g_sta_upgrade_timer, BEARER_STA_UPGRADE_TIMEOUT));
                } else {
                    ESP_LOGI(TAG, "sta upgrade timer restart, %d", __LINE__);
                    esp_timer_stop(g_sta_upgrade_timer);
                    esp_timer_start_once(g_sta_upgrade_timer, BEARER_STA_UPGRADE_TIMEOUT);
                }
            }
        } else {
            if (get_client_socket(event->mac) == -1) {
                /* One situation here is that:
                 * A station is connected, and before we succeed to accept() a proper socket the station
                 * is disconnected. When the second "SYSTEM_EVENT_AP_STACONNECTED" event comes the
                 * station will be treated as a re-connect one, and we only have to continue with the
                 * previous accept() operation, no need to accpet() again.
                 */
                break;
            }
        }
        msg.flag = BLE_MESH_OTA_AP_GOT_STA;
        memcpy(msg.addr, event->mac, BLE_MESH_OTA_DEV_ADDR_LEN);
        if (ble_mesh_ota_wifi_task_post(&msg, portMAX_DELAY, false) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post AP_GOT_STA event");
        }
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
        ESP_LOGI(TAG, "Station mac: "MACSTR"", MAC2STR(event->mac));
        ESP_LOGD(TAG, "Station aid: %d", event->aid);
        break;
    }
    default:
        break;
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    ENTER_FUNC();
    ble_mesh_ota_msg_t msg = {0};

    switch(event_id) {
    case IP_EVENT_STA_GOT_IP: {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
        ESP_LOGD(TAG, "IP: "IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGD(TAG, "Netmask: "IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGD(TAG, "Gateway: "IPSTR, IP2STR(&event->ip_info.gw));
        g_tcp_ctx.client.ip.addr = event->ip_info.ip.addr;
        g_tcp_ctx.client.netmask.addr = event->ip_info.netmask.addr;
        g_tcp_ctx.client.gw.addr = event->ip_info.gw.addr;
        msg.flag = BLE_MESH_OTA_STA_GOT_AP;
        memcpy(msg.addr, g_tcp_ctx.client.bssid, BLE_MESH_OTA_DEV_ADDR_LEN);
        if (ble_mesh_ota_wifi_task_post(&msg, portMAX_DELAY, false) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post STA_GOT_AP event");
        }
        break;
    }
    case IP_EVENT_STA_LOST_IP:
        ESP_LOGE(TAG, "IP_EVENT_STA_LOST_IP");
        break;
    case IP_EVENT_AP_STAIPASSIGNED:
        ESP_LOGI(TAG, "IP_EVENT_AP_STAIPASSIGNED");
        break;
    default:
        break;
    }
}

esp_err_t ble_mesh_ota_wifi_task_post(ble_mesh_ota_msg_t *msg, uint32_t timeout, bool to_front)
{
    ENTER_FUNC();
    BaseType_t ret = pdTRUE;

    if (to_front) {
        ret = xQueueSendToFront(g_wifi_queue, msg, timeout);
    } else {
        ret = xQueueSend(g_wifi_queue, msg, timeout);
    }

    return (ret == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ota_tcp_task_post(ble_mesh_ota_msg_t *msg, uint32_t timeout)
{
    ENTER_FUNC();
    BaseType_t ret = pdTRUE;

    ret = xQueueSend(g_tcp_queue, msg, timeout);

    return (ret == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ble_mesh_ota_from_url(char *ota_url, uint32_t *length)
{
    ENTER_FUNC();
    esp_err_t ret = ESP_OK;
    esp_http_client_config_t config = {
        .url = ota_url,
        .buffer_size = 2 * 1024,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .timeout_ms = 10 * 1000,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        return ESP_FAIL;
    }

    while (1) {
        ret = esp_https_ota_perform(https_ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        ESP_LOGD(TAG, "Image bytes read: %d", esp_https_ota_get_image_len_read(https_ota_handle));
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG, "Complete data was not received.");
    } else {
        ESP_LOGI(TAG, "Image length: %d", esp_https_ota_get_image_len_read(https_ota_handle));
        *length = esp_https_ota_get_image_len_read(https_ota_handle);
    }

    ret = esp_https_ota_finish(https_ota_handle);

    return ret;
}

static void wifi_task(void *param)
{
    ENTER_FUNC();
    ble_mesh_ota_msg_t msg    = {0};
    static uint8_t send_count = 0;
    static uint16_t count     = 0;
    /* If this task gets a tcp send failure (e.g. due to station disconnection)
     * and stops sending OTA segments, it can posts an event to notify ota task.
     * When station is reconnected, this task can post an event to notify ota
     * task to continue sending ota segments and clear the "stop_seg_send" flag.
     */
    static bool stop_seg_send;

    while (1) {
        if (xQueueReceive(g_wifi_queue, &msg, portMAX_DELAY)) {
            switch (msg.flag) {
            case BLE_MESH_OTA_AP_GOT_STA: {
                ESP_LOGI(TAG, "SoftAP connected with a station");
                bool new_dev;
                tcp_server_accept(&new_dev);
                if (g_wifi_init.ota_done && stop_seg_send && new_dev == false) {
                    ESP_LOGW(TAG, "Reconnected station "MACSTR"", MAC2STR(msg.addr));
                    msg.flag = BLE_MESH_OTA_SEG_SEND_CONT;
                    ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);
                }
                break;
            }
            case BLE_MESH_OTA_STA_GOT_AP: {
                ESP_LOGI(TAG, "WiFi Station is connected to SoftAP");
                if (g_wifi_init.wifi_role == BLE_MESH_OTA_WIFI_STA_FROM_URL) { // download firmware from server
                    // start download firmware from server
                    ESP_LOGI(TAG, "start download firmware from server");
                    if (ble_mesh_ota_from_url(g_wifi_init.ota_url, &msg.length) == ESP_OK) {
                        ESP_LOGI(TAG, "download firmware from server finished, length: %d", msg.length);
                        ESP_LOGI(TAG, "Change to AP Mode to send firmware to other need ota device");
                        msg.flag = BLE_MESH_OTA_DOWN_URL_DONE;
                        ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);
                    } else {
                        ESP_LOGE(TAG, "BLE Mesh OTA download firmware error from server specified url!");
                        msg.flag = BLE_MESH_OTA_FAIL;
                        ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);
                    }
                } else if (g_wifi_init.wifi_role == BLE_MESH_OTA_WIFI_STA) {
                    bool new_dev;
                    tcp_client_connect(msg.addr, &new_dev);
                    if (new_dev == false) {
                        ESP_LOGW(TAG, "Reconnected with SoftAP "MACSTR"", MAC2STR(msg.addr));
                    }
                }
                break;
            }
            case BLE_MESH_OTA_SEG_SEND_CONT:
                ESP_LOGD(TAG, "BLE_MESH_OTA_SEG_SEND_CONT, %d", __LINE__);
                stop_seg_send = false;
                break;
            case BLE_MESH_OTA_SEG_SEND:
            case BLE_MESH_OTA_PDU_SEND: {
                if (msg.flag == BLE_MESH_OTA_SEG_SEND && stop_seg_send && msg.data) {
                    ESP_LOGW(TAG, "Ignore sending OTA segment 0x%04x", msg.data[7] << 8 | msg.data[6]);
                    free(msg.data);
                    break;
                }

                if ((msg.data[7] << 8 | msg.data[6]) % 20 == 0) {
                    ESP_LOGD(TAG, "sta upgrade timer restart, %d", __LINE__);
                    esp_timer_stop(g_sta_upgrade_timer);
                    esp_timer_start_once(g_sta_upgrade_timer, BEARER_STA_UPGRADE_TIMEOUT);
                }

                ble_mesh_ota_tx_ctx_t tx = {
                    .data = msg.data,
                    .length = msg.length,
                };
                memcpy(tx.addr, msg.addr, BLE_MESH_OTA_DEV_ADDR_LEN);
                if (tcp_send(&tx) != ESP_OK) {
                    if (msg.flag == BLE_MESH_OTA_SEG_SEND) {
                        uint16_t fail_seg = (msg.data[7] << 8 | msg.data[6]);
                        ESP_LOGE(TAG, "Failed to send OTA segment 0x%04x", fail_seg);
                        /* Notifies OTA task to stop sending OTA segments to the device */
                        ble_mesh_ota_msg_t stop_msg = {
                            .flag = BLE_MESH_OTA_SEG_SEND_STOP,
                            .data = (uint8_t *)&fail_seg,
                            .length = sizeof(fail_seg),
                        };
                        memcpy(stop_msg.addr, msg.addr, BLE_MESH_OTA_DEV_ADDR_LEN);
                        if (ble_mesh_ota_op_task_post(&stop_msg, portMAX_DELAY, false) != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to post OTA_SEG_SEND_STOP event");
                            break;
                        }
                        send_count = 0;
                        stop_seg_send = true;
                    } else {
                        ESP_LOGE(TAG, "Failed to send OTA ACK pdu");
                        /* TODO: Notify OTA task to resend OTA ACK pdu */
                    }
                    if (msg.data) {
                        free(msg.data);
                    }
                    break;
                }
                /* The msg.ctx is allocated by OTA module which is used to store 1K octets OTA segment or OTA pdu */
                if (msg.flag == BLE_MESH_OTA_SEG_SEND) {
                    /* 4 octets opcode + 2 octets total seg count + 2 octets current seg seq + 1024 octets OTA segment */
                    ESP_LOGD(TAG, "BLE_MESH_OTA_SEG_SEND, %d, total seg count: %d, current seg seq: %d", __LINE__, msg.data[5] << 8 | msg.data[4], msg.data[7] << 8 | msg.data[6]);
                    if (++send_count == MIN(g_sta_conn_count, ble_mesh_get_ota_dev_count())) {
                        ESP_LOGD(TAG, "count: %d, BLE_MESH_OTA_SEG_SEND, %d", count++, __LINE__);
                        if (msg.data) {
                            ESP_LOGD(TAG, "free count: %d", count++);
                            free(msg.data);
                        }
                        send_count = 0;
                    }
                } else {
                    ESP_LOGD(TAG, "BLE_MESH_OTA_PDU_SEND, change ota done, %d", __LINE__);
                    /* If the device is a need-OTA device & AP, and it has been updated successfully,
                     * then it will start ESP-NOW to update other devices.
                     */
                    if (msg.data) {
                        free(msg.data);
                    }
                    /* TODO: check if all OTA segments are received, if not post a event to start recv() */
                    msg.flag = BLE_MESH_OTA_PROC_DONE;
                    ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);

                    // close(get_tcp_socket(msg.addr)); /* close the socket */
                    g_wifi_init.ota_done = true; /* The device has been updated */
                }
                break;
            }
            case BLE_MESH_OTA_SEG_RECV:
            case BLE_MESH_OTA_PDU_RECV:
                if (msg.flag == BLE_MESH_OTA_SEG_RECV) {
                    ESP_LOGD(TAG, "BLE_MESH_OTA_SEG_RECV, %d", __LINE__);
                    if (ble_mesh_store_send_ota_seg_device(msg.addr) != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to store sending OTA segment device address");
                        break;
                    }
                } else {
                    ESP_LOGD(TAG, "BLE_MESH_OTA_PDU_RECV, %d", __LINE__);
                }
                if (ota_tcp_task_post(&msg, portMAX_DELAY) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to post event %d", msg.flag);
                }
                break;
            case BLE_MESH_OTA_PROC_DONE: {
                /* If finish updating some devices, we will not restart immediately and will wait 
                 * a few more seconds to check if we can get other need-ota device(s).
                 */
                ESP_LOGD(TAG, "BLE_MESH_OTA_PROC_DONE, %d", __LINE__);
                for (int i = 0; i < g_tcp_ctx.server.max_client; i++) {
                    struct tcp_client *client = &g_tcp_ctx.server.clients[i];
                    memset(client, 0x0, offsetof(struct tcp_client, sockfd));
                    if (client->sockfd != -1) {
                        close(client->sockfd);
                        client->sockfd = -1;
                    }
                }
                break;
            }
            case BLE_MESH_OTA_WIFI_INIT:{
                ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_INIT, %d", __LINE__);
                ble_mesh_ota_wifi_init((ble_mesh_ota_wifi_init_t *)msg.data);
                free(msg.data);
                break;
            }
            case BLE_MESH_OTA_MESH_START:{
                break;
            }
            case BLE_MESH_OTA_MESH_STOP:{
                break;
            }
            case BLE_MESH_OTA_MESH_NBVN:{
                break;
            }
            case BLE_MESH_OTA_WIFI_TASK_DELETE:{
                vQueueDelete(g_wifi_queue);
                vTaskDelete(NULL);
                break;
            }
            default:
                break;
            }
        }
    }

    ESP_LOGE(TAG, "Delete ota wifi task");
    vTaskDelete(NULL);
}

static void tcp_task(void *param)
{
    ENTER_FUNC();
    static uint16_t count = 0;
    int sockfd, expect_len, actual_recv, recv_len = 0;
    uint16_t max_seg = 0, curr_seg = 0;
    ble_mesh_ota_msg_t msg       = {0};
    uint8_t            *recv_buf = NULL;
    bool               recv_fail = false;

    while (1) {
        if (xQueueReceive(g_tcp_queue, &msg, portMAX_DELAY)) {
            switch (msg.flag) {
            case BLE_MESH_OTA_SEG_RECV:
            case BLE_MESH_OTA_PDU_RECV:
                if (msg.flag == BLE_MESH_OTA_PDU_RECV) {
                    uint16_t seg_count = ble_mesh_get_ota_seg_count();
                    expect_len = BLE_MESH_OTA_OPCODE_LEN + (seg_count % 8 ? seg_count / 8 + 1 : seg_count / 8);
                } else {
                    expect_len = BLE_MESH_OTA_SEG_HEADER_SIZE + CONFIG_BLE_MESH_OTA_SEGMENT_SIZE + BLE_MESH_OTA_SEG_HASH_SIZE;
                }
                sockfd = get_tcp_socket(msg.addr);
                if (sockfd < 0) {
                    ESP_LOGE(TAG, "Failed to get TCP socket");
                    break;
                }
                while (1) {
                    recv_len = 0;
                    ESP_LOGD(TAG, "malloc count: %d, %d", count++, __LINE__);
                    recv_buf = calloc(1, expect_len);
                    if (!recv_buf) {
                        ESP_LOGE(TAG, "Failed to allocate memory");
                        break;
                    }
                    while (1) {
                        actual_recv = recv(sockfd, recv_buf + recv_len, expect_len - recv_len, 0);
                        if (actual_recv < 0) {
                            tcp_error("TCP recv()", sockfd);
                            close(sockfd);
                            free(recv_buf);
                            recv_fail = true;
                            break;
                        } else {
                            if (actual_recv == 0) {
                                ESP_LOGW(TAG, "recv() return 0");
                            }
                            recv_len += actual_recv;
                            if (actual_recv == 0 || recv_len == expect_len) {
                                break;
                            }
                        }
                    }
                    if (recv_fail == true) {
                        break;
                    }
                    if (msg.flag == BLE_MESH_OTA_SEG_RECV) {
                        if (!max_seg) {
                            max_seg = (recv_buf[5] << 8 | recv_buf[4]);
                        }
                        curr_seg = (recv_buf[7] << 8 | recv_buf[6]);
                    }
                    msg.data   = recv_buf;
                    msg.length = recv_len;
                    ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);

                    if (msg.flag == BLE_MESH_OTA_SEG_RECV) {
                        if (max_seg == curr_seg) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                break;
            case BLE_MESH_OTA_TCP_TASK_DELETE:
                vQueueDelete(g_tcp_queue);
                vTaskDelete(NULL);
                break;
            default:
                break;
            }
        }
    }

    ESP_LOGE(TAG, "Delete ota tcp task");
    vTaskDelete(NULL);
}

static esp_err_t generate_wifi_info(ble_mesh_ota_wifi_init_t *init)
{
    ENTER_FUNC();
    if (!init) {
        ESP_LOGE(TAG, "Invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    /* This solution has bug, may cause two AP with the same ssid & password. */
    for (uint8_t i = 0; i < sizeof(init->ssid); i++) {
        if (init->ssid[i] < 0x21 || init->ssid[i] > 0x7E) {
            init->ssid[i] = (init->ssid[i] % 26) + 0x61;
        }
    }

    for (uint8_t i = 0; i < sizeof(init->password); i++) {
        if (init->password[i] < 0x21 || init->password[i] > 0x7E) {
            init->password[i] = (init->password[i] % 26) + 0x61;
        }
    }

    /* Need to find a way. */
    for (uint8_t i = 0; i < sizeof(init->ssid); i++) {
        if (init->ssid[i] == 0x00) {
            init->ssid[i] = 0x02;
        }
    }
    for (uint8_t i = 0; i < sizeof(init->password); i++) {
        if (init->password[i] == 0x00) {
            init->password[i] = 0xE5;
        }
    }

    return ESP_OK;
}

static void sta_conn_timeout_cb(void *arg)
{
    ENTER_FUNC();

    /* May disable Wi-Fi functionality, and back to normal BLE Mesh model. */
    ESP_LOGW(TAG, "No station connected ... ...");

    ble_mesh_ota_msg_t msg = {0};
    msg.flag = BLE_MESH_OTA_FAIL;
    ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);

    esp_restart();
}

/**
 * wifi task create
*/
esp_err_t ble_mesh_ota_wifi_task_init(void)
{
    ENTER_FUNC();
    esp_err_t ret = ESP_OK;
    static bool wifi_task_init = false;

    if (wifi_task_init) {
        return -EALREADY;
    }

    g_wifi_queue = xQueueCreate(5, sizeof(ble_mesh_ota_msg_t));
    assert(g_wifi_queue);

    g_tcp_queue = xQueueCreate(5, sizeof(ble_mesh_ota_msg_t));
    assert(g_tcp_queue);

    ret = xTaskCreatePinnedToCore(wifi_task, "wifi_task", 4096, NULL, 5, NULL, 1);
    assert(ret == pdTRUE);

    ret = xTaskCreatePinnedToCore(tcp_task, "tcp_task", 4096, NULL, 5, NULL, 1);
    assert(ret == pdTRUE);

    wifi_task_init = true;

    return ESP_OK;
}

/**
 * wifi task deinit
*/
esp_err_t ble_mesh_ota_wifi_task_deinit(void)
{
    ENTER_FUNC();
    ble_mesh_ota_msg_t msg = {0};

    msg.flag = BLE_MESH_OTA_WIFI_TASK_DELETE;
    ble_mesh_ota_wifi_task_post(&msg, portMAX_DELAY, false);

    msg.flag = BLE_MESH_OTA_TCP_TASK_DELETE;
    ota_tcp_task_post(&msg, portMAX_DELAY);

    return ESP_OK;
}

/** wifi status change 
 * 1. AP(Ready to Recv or Start to Send)
 * 2. Sta(Ready to Recv)
*/
esp_err_t ble_mesh_ota_wifi_init(ble_mesh_ota_wifi_init_t *init)
{
    ENTER_FUNC();
    wifi_config_t config = {0};

    switch (init->wifi_role) {
    case BLE_MESH_OTA_WIFI_STA:
        ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_STA, %d", __LINE__);
        memset(&g_tcp_ctx.client, 0, sizeof(g_tcp_ctx.client));
        g_tcp_ctx.client.sockfd = -1;
        break;
    case BLE_MESH_OTA_WIFI_AP:
        ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_AP, %d", __LINE__);
        g_tcp_ctx.server.max_client = init->max_conn;
        for (int i = 0; i < init->max_conn; i++) {
            struct tcp_client *client = &g_tcp_ctx.server.clients[i];
            memset(client, 0, sizeof(struct tcp_client));
            client->sockfd = -1;
        }
        g_tcp_ctx.server.sockfd = -1;
        break;
    case BLE_MESH_OTA_WIFI_STA_FROM_URL:
        ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_STA_FROM_URL, %d", __LINE__);
        break;
    default:
        ESP_LOGE(TAG, "Unsupported WiFi role %d", init->wifi_role);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "generate_wifi_info before");
    if (init->wifi_role != BLE_MESH_OTA_WIFI_STA_FROM_URL) {
        if (generate_wifi_info(init) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to generate ssid & password");
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "Generate ssid: %s, password: %s", init->ssid, init->password);
    }

    ESP_LOGI(TAG, "Initializing BLE Mesh OTA WiFi-%s", init->wifi_role == BLE_MESH_OTA_WIFI_AP ? "SoftAP" : "STA");

    memcpy(&g_wifi_init, init, sizeof(ble_mesh_ota_wifi_init_t));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    switch (init->wifi_role) {
    case BLE_MESH_OTA_WIFI_STA_FROM_URL:
        memcpy(config.sta.ssid, init->url_ssid, sizeof(init->url_ssid));
        memcpy(config.sta.password, init->url_pass, sizeof(init->url_pass));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &config));
        ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_STA_FROM_URL ssid: %s, password: %s, url: %s", config.sta.ssid, config.sta.password, g_wifi_init.ota_url);
        break;
    case BLE_MESH_OTA_WIFI_STA:
        memcpy(config.sta.ssid, init->ssid, sizeof(init->ssid));
        memcpy(config.sta.password, init->password, sizeof(init->password));
        config.sta.channel = init->ssid[2] % 12;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &config));
        ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_STA ssid: %s, password: %s, channel: %d", config.sta.ssid, config.sta.password, config.sta.channel);
        break;
    case BLE_MESH_OTA_WIFI_AP:
        memcpy(config.ap.ssid, init->ssid, BLE_MESH_OTA_WIFI_SSID_LEN);
        memcpy(config.ap.password, init->password, BLE_MESH_OTA_WIFI_PWD_LEN);
        config.ap.ssid_len = BLE_MESH_OTA_WIFI_SSID_LEN;
        config.ap.channel  = init->ssid[2] % 12;
        /* Currently we ignore whether the device has been updated. */
        config.ap.max_connection = init->max_conn;
        config.ap.authmode       = WIFI_AUTH_WPA_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &config));
        ESP_LOGI(TAG, "BLE_MESH_OTA_WIFI_AP ssid: %s, password: %s, channel: %d", config.ap.ssid, config.ap.password, config.ap.channel);
        break;
    default:
        break;
    }

    esp_mesh_set_6m_rate(true);

    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

/**
 * wifi deinit
*/
esp_err_t ble_mesh_ota_wifi_deinit(void)
{
    ENTER_FUNC();
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    return ESP_OK;
}

/* netif and event loop init */
esp_err_t ble_mesh_ota_netif_init(void)
{
    ENTER_FUNC();
    static bool netif_init = false;

    if (netif_init) {
        ESP_LOGW(TAG, "ble mesh ota netif init already init");
        return -EALREADY;
    }

    ESP_LOGD(TAG, "ble mesh ota netif init init");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));

    netif_init = true;

    return ESP_OK;
}

/* netif and event loop init */
esp_err_t ble_mesh_ota_netif_deinit(void)
{
    ENTER_FUNC();
    // Note: Deinitialization is not supported yet
    esp_netif_deinit();

    return ESP_OK;
}

/**
 * post event to wifi tast to 
*/
esp_err_t ble_mesh_ota_wifi_init_post(ble_mesh_ota_wifi_init_t *init)
{
    ENTER_FUNC();
    ble_mesh_ota_wifi_task_init();

    ble_mesh_ota_msg_t msg = {0};

    msg.flag   = BLE_MESH_OTA_WIFI_INIT;
    msg.length = sizeof(ble_mesh_ota_wifi_init_t);
    msg.data   = malloc(sizeof(ble_mesh_ota_wifi_init_t));
    memcpy(msg.data, init, sizeof(ble_mesh_ota_wifi_init_t));

    return ble_mesh_ota_wifi_task_post(&msg, portMAX_DELAY, false);
}

/** wifi status change 
 * 1. AP(Ready to Recv or Start to Send)
 * 2. Sta(Read to Recv)
*/
esp_err_t ble_mesh_ota_bearer_init(ble_mesh_ota_wifi_init_t *init)
{
    ENTER_FUNC();
    static bool wifi_initialize = false;

    ble_mesh_ota_wifi_task_init();

    if (wifi_initialize) {
        ble_mesh_ota_wifi_deinit();
    }

    if (!init || !init->max_conn || (init->max_conn > BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM)) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    ble_mesh_ota_netif_init();

    ble_mesh_ota_wifi_init(init);

    if (init->ota_done && g_sta_conn_timer == NULL) {
        /* If the device is used to upgrade other devices, and during initialization,
         * we will start a timer, in case no station is connected for a long time.
         */
        esp_timer_create_args_t args = {
            .callback = &sta_conn_timeout_cb,
            .name     = "g_sta_conn_timer",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &g_sta_conn_timer));
        ESP_ERROR_CHECK(esp_timer_start_once(g_sta_conn_timer, BEARER_STA_CONN_TIMEOUT));
    }

    wifi_initialize = true;

    return ESP_OK;
}
