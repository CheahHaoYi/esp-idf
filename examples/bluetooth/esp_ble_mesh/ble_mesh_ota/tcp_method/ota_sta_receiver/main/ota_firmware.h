#pragma once

#include "esp_err.h"
#include "esp_log.h"

#include "esp_netif.h"

#include <inttypes.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define PORT CONFIG_EXAMPLE_PORT
#define TCP_BUFFER_SIZE CONFIG_LWIP_TCP_MSS

#define RX_BUFFER_SIZE (sizeof(ota_data_packet_t))

enum {
    PACKET_TYPE_DATA = 0x00,
    PACKET_TYPE_INFO = 0x01,
    PACKET_TYPE_START = 0x02,
    PACKET_TYPE_END = 0x03,
};

typedef struct {
    uint8_t type;                               /**< Type of packet, PACKET_TYPE_DATA */
    uint16_t seq;                               /**< Sequence */
    uint16_t size;                               /**< Size */
    uint8_t data[TCP_BUFFER_SIZE - 5];   /**< Firmware */
} __attribute__((packed)) ota_data_packet_t;

typedef struct {
    uint8_t type;                               /**< Type of packet, PACKET_TYPE_INFO */
    uint32_t total_size;                        /**< Total size of firmware */
    uint8_t sha_256[32];                        /**< SHA-256 of firmware */
} __attribute__((packed)) ota_info_packet_t;

esp_err_t ota_connection_init(void);

esp_err_t ota_connection_deinit(void);


