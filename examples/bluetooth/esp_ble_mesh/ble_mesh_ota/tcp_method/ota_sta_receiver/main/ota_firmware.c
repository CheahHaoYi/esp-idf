#include "ota_firmware.h"

#define TAG "ota_firmware"
#define SERVER_IP_ADDR "192.168.4.1"

static int softap_socket = 0;

// static const char payload[] = "Hello from ESP";

int socket_init(char ip_addr[]) {
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr;
    inet_pton(addr_family, ip_addr, &dest_addr.sin_addr);
    dest_addr.sin_family = addr_family;
    dest_addr.sin_port = htons(PORT);

    int sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return sock;
    }
    ESP_LOGI(TAG, "Socket created, connecting to %s:%d", ip_addr, PORT);

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
        return -1;
    }
    ESP_LOGI(TAG, "Successfully connected");

    return sock;
}

esp_err_t socket_deinit(void) 
{
    ESP_LOGE(TAG, "Shutting down socket and restarting...");

    if (softap_socket > 0) {
        shutdown(softap_socket, 0);
        close(softap_socket);
    }

    return ESP_OK;
}

esp_err_t process_data_packet(ota_data_packet_t *packet) 
{
    ESP_LOGI(TAG, "Received data packet, seq: %" PRIu16 ", size: %" PRIu16, packet->seq, packet->size);
    
    ESP_LOGI(TAG, "First 10 characters: ");

    for(int i = 0; i < 10; i++) {
        printf("%c", packet->data[i]);
    } printf("\n");

    return ESP_OK;
}

esp_err_t process_info_packet(ota_info_packet_t *packet) 
{
    ESP_LOGI(TAG, "Received info packet, total_size: %" PRIu32, packet->total_size);
    return ESP_OK;
}

esp_err_t ota_connection_init(void) 
{
    char rx_buffer[RX_BUFFER_SIZE];
    char host_ip[] = SERVER_IP_ADDR;
    int rx_fill_offset = 0;

    softap_socket = socket_init(host_ip);
    if (softap_socket < 0) {
        ESP_LOGE(TAG, "Setup socket failed: errno %d", errno);
        return ESP_FAIL;
    }

    while (1) {

        int len = recv(softap_socket, rx_buffer + rx_fill_offset, sizeof(rx_buffer), 0);
        // Error occurred during receiving
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d" , errno);
            break;
        }
        // Data received

        rx_fill_offset += len;

        rx_buffer[rx_fill_offset] = 0; // Null-terminate whatever we received and treat like a string
        ESP_LOGI(TAG, "Received %d bytes from %s, offset %d", len, host_ip, rx_fill_offset);

        uint8_t packet_type = ((uint8_t *)rx_buffer)[0];
        
        if (rx_fill_offset >= sizeof(ota_data_packet_t) && packet_type == PACKET_TYPE_DATA) {
            process_data_packet((ota_data_packet_t *)rx_buffer);
            rx_fill_offset = 0;
        } else if (rx_fill_offset >= sizeof(ota_info_packet_t) && packet_type == PACKET_TYPE_INFO) {
            process_info_packet((ota_info_packet_t *)rx_buffer);
            rx_fill_offset = 0;
        }
    }

    socket_deinit();    

    return ESP_OK;
}


