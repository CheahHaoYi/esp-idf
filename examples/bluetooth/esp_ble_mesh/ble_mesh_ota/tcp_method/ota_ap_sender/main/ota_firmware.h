#pragma once

#include "esp_err.h"
#include "esp_log.h"

#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_ota_ops.h"
#include "esp_http_client.h"


#define HTTP_READ_BUFFER_LEN 1400
#define HTTP_CONNECT_ATTEMPT_MAX_COUNT 20


#define TCP_BUFFER_SIZE CONFIG_LWIP_TCP_MSS

#define PORT                        CONFIG_EXAMPLE_PORT
#define KEEPALIVE_IDLE              CONFIG_EXAMPLE_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_EXAMPLE_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_EXAMPLE_KEEPALIVE_COUNT

#define LISTEN_BACKLOG  1

#define ESP_ERROR_GOTO(con, lable, format, ...) do { \
        if (con) { \
            if(*format != '\0') \
                ESP_LOGW(TAG, "[%s, %d]" format, __func__, __LINE__, ##__VA_ARGS__); \
            goto lable; \
        } \
    } while(0)

#define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS)

enum {
    PACKET_TYPE_DATA = 0x00,
    PACKET_TYPE_INFO,
    PACKET_TYPE_START,
    PACKET_TYPE_END,
};

typedef struct {
    uint8_t type;                               /**< Type of packet, PACKET_TYPE_DATA */
    uint16_t seq;                               /**< Sequence */
    uint16_t size;                               /**< Size */
    uint8_t data[(TCP_BUFFER_SIZE - 5)];   /**< Firmware */
} __attribute__((packed)) ota_data_packet_t;

typedef struct {
    uint8_t type;                               /**< Type of packet, PACKET_TYPE_INFO */
    uint32_t total_size;                        /**< Total size of firmware */
    uint8_t sha_256[32];                        /**< SHA-256 of firmware */
} __attribute__((packed)) ota_info_packet_t;

size_t sta_fetch_firmware(const char* url);

esp_err_t ap_send_firmware_start(void);

esp_err_t ap_send_firmware_task(int sock);

esp_err_t accept_client_connection(void);

esp_err_t ap_clean_up(void);

