#include "ota_firmware.h"

#define TAG "ota_firmware"

static int listen_sock = 0;
static int accept_sock = 0;

esp_err_t establish_client_connection(esp_http_client_handle_t client)
{
    /**
     * @brief First, the firmware is obtained from the http server and stored
     */

    esp_err_t ret = ESP_OK;

    int attempt_count = 0;
    while (attempt_count++ < HTTP_CONNECT_ATTEMPT_MAX_COUNT) {
        ret = esp_http_client_open(client, 0);

        if (ret != ESP_OK) {
            DELAY(1000);
            ESP_LOGW(TAG, "<%s> Connection service failed", esp_err_to_name(ret));
        }
    } 
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Connection service success");
    }

    return ret;
}

esp_err_t write_firmware_to_flash(esp_http_client_handle_t client, esp_ota_handle_t ota_handle, size_t firmware_size)
{
    uint8_t data[HTTP_READ_BUFFER_LEN] = {0};
    esp_err_t ret = ESP_OK;

    for (ssize_t size = 0, recv_size = 0; recv_size < firmware_size; recv_size += size) {
        size = esp_http_client_read(client, (char *)data, HTTP_READ_BUFFER_LEN);
        if (size <= 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            return ESP_FAIL;
        }

        ret = esp_ota_write(ota_handle, (const void *)data, size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error: OTA write error! err=0x%x", ret);
            return ret;
        }
    }

    return ret;
}

// get ota firmware from server
size_t sta_fetch_firmware(const char* url) {

    // assert wifi mode

    esp_err_t ret = ESP_OK;
    size_t total_size = 0;
    esp_ota_handle_t ota_handle = 0;
    uint32_t start_time = xTaskGetTickCount();

    esp_http_client_config_t config = {
        .url = url,
        .transport_type = HTTP_TRANSPORT_UNKNOWN,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_ERROR_GOTO(!client, EXIT, "Initialise HTTP connection");

    ESP_LOGI(TAG, "Open HTTP connection: %s", url);

    if (establish_client_connection(client) != ESP_OK) {
        goto EXIT;
    }

    total_size = esp_http_client_fetch_headers(client);

    if (total_size <= 0) {
        ESP_LOGE(TAG, "Invalid content length");
        goto EXIT;
    }
    ESP_LOGI(TAG, "Total binary data length: %d", total_size);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    ret = esp_ota_begin(update_partition, total_size, &ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_begin failed, total_size", esp_err_to_name(ret));

    // write_firmware_to_flash(client, ota_handle, total_size);

    uint8_t data[HTTP_READ_BUFFER_LEN] = {0};
    // esp_err_t ret = ESP_OK;

    for (ssize_t size = 0, recv_size = 0; recv_size < total_size; recv_size += size) {
        size = esp_http_client_read(client, (char *)data, HTTP_READ_BUFFER_LEN);
        if (size <= 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            break;
        }
        // ESP_LOG_BUFFER_HEX(TAG, data, size);
        ESP_LOGI(TAG, "Read data size: %d, recv_size: %d, total_size: %d", size, recv_size, total_size);
        ret = esp_ota_write(ota_handle, (const void *)data, size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error: OTA write error! err=0x%x", ret);
            break;
        }
    }

    ESP_LOGI(TAG, "The service download firmware is complete, Spend time: %" PRIu32 "s",
        (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS / 1000);

    ret = esp_ota_end(ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_end", esp_err_to_name(ret));

EXIT:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return total_size;
}

esp_err_t ap_clean_up(void) {
    if (accept_sock > 0) {
        shutdown(accept_sock, 0);
        close(accept_sock);
        ESP_LOGI(TAG, "Closed accept socket");
    }
    
    if (listen_sock > 0) {
        close(listen_sock);
        ESP_LOGI(TAG, "Closed listen socket");
    }
    return ESP_OK;
}

// send the whole ota firmware to a client
esp_err_t ap_send_firmware_task(int sock) {
    int msg_len = 0;
    // char rx_buffer[TCP_BUFFER_SIZE];

    ota_data_packet_t test_data = {
        .type = PACKET_TYPE_DATA,
        .seq = 0,
        .size = 11,
        .data = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'},
    };

    ota_info_packet_t test_info = {
        .type = PACKET_TYPE_INFO,
        .total_size = 2,
        .sha_256 = {'H', 'i'},
    };

    int i = 0;
    do {
        // msg_len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);

        // if (msg_len <= 0) {
        //     break;
        // }

        // // Data received
        // rx_buffer[msg_len] = 0; // Null-terminate whatever we received and treat like a string
        // ESP_LOGI(TAG, "Received %d bytes: %s", msg_len, rx_buffer);
        void *data_ptr;
        int send_len = 0;
        if (i %2 == 0) {
            test_data.seq = i;
            send_len = sizeof(test_data);
            data_ptr = &test_data;
            ESP_LOGI(TAG, "Sending data packet, seq %d", test_data.seq);
        } else {
            send_len = sizeof(test_info);
            data_ptr = &test_info;
        }

        int written = send(sock, data_ptr, send_len, 0);
        if (written < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Sent %d bytes, seq %d", written, i);
        DELAY(5000);
    } while (i ++ < 10);

    if (msg_len < 0) {
        ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
    } else if (msg_len == 0) {
        ESP_LOGW(TAG, "Connection closed");
    } 

    return ESP_OK;
}

esp_err_t set_socket_properties(int sock) {
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;

    // allow reuse of local address
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

    return ESP_OK;
}

esp_err_t accept_client_connection(void) {
    char addr_str[128] = {0};

    while (1) {
        ESP_LOGI(TAG, "Waiting for client connection...");

        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int conn_sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (conn_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno 0x%x", errno);
            break;    
        } 
        
        ESP_LOGI(TAG, "New client connected");
        set_socket_properties(conn_sock);

        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        } else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG, "Client IP address: %s", addr_str);

        ap_send_firmware_task(conn_sock);

        shutdown(conn_sock, 0);
        close(conn_sock);
    }
    return ESP_OK;
}


// send firmware to client, init socket and resources
// support ipv4 for now...
esp_err_t ap_send_firmware_start(void) {
    struct sockaddr_storage dest_addr;

    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT);
    
    listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno 0x%x", errno);
        return ESP_FAIL;
    }

    int opt_set = 1; 
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt_set, sizeof(opt_set));
    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno 0x%x", errno);
        goto CLEAN_UP;
    }

    err = listen(listen_sock, LISTEN_BACKLOG);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno 0x%x", errno);
        goto CLEAN_UP;
    }

    return ESP_OK;

CLEAN_UP:
    close(listen_sock);
    return ESP_FAIL;
}