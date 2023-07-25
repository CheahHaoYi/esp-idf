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

#include "mbedtls/aes.h"
#include "mbedtls/cipher.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/cmac.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_now.h"
#include "esp_ota_ops.h"

#include "ble_mesh_ota_op.h"
#include "ble_mesh_ota_utility.h"

#define TAG "ota_op"

/* 2 * START_OTA_SEND_TIMEOUT_US < BEARER_STA_UPGRADE_TIMEOUT */
#define START_OTA_SEND_TIMEOUT_US (30 * 1000 * 1000) /* 30s */

typedef struct ota_device {
    uint8_t  addr[6];       /* Address of the device, currently is got from WiFi events */
    uint16_t last_seg;      /* Sequence number of the last sent segment (zero-based) to the device (mainly used when the device is disconnected unexpectedly) */
    bool     send_stop;     /* Indicate if the send operation shall be stopped due to connection failure */
} ota_device_t;

static struct ota_ctx_t {
    const esp_partition_t *ota_partition;
    esp_ota_handle_t ota_handle;
    uint8_t  ota_op;            /* OTA operation of the device: send or receive OTA segments */
    uint32_t total_bin_size;    /* Size of all the OTA segments */
    uint16_t max_seg_seq;       /* Maximum sequence number of all OTA segments (zero-based), actually is max seg seq + 1 */
    struct {
        struct {
            uint8_t  addr[6];           /* Own device address (STA) or peer device address (SOFTAP) */
            uint16_t last_seg_recv;     /* Sequence number of the last received segment (zero-based) */
            uint16_t seg_valid_count;   /* Number of received invalid segments */
            uint8_t *seg_ack_array;     /* ACK array of all the segments, each bit indicate one segment */
            bool     first_time_recv;   /* Indicate if the device receives the segments for the first time or receive some retransmitted segments */
        } recv; /* Role of the device which receives OTA segments (self can be SoftAP or STA) */
        struct {
            uint8_t max_device;         /* Number of peer devices can send OTA segments to */
            ota_device_t devices[BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM];
            uint8_t  ota_dev_count;     /* Number of devices going to send OTA segments to */
            bool     update_ready;      /* Indicate if it is ready to update other devices */
            uint16_t last_seg_sent;     /* Sequence number of the last sent segment (zero-based) to all devices */
            uint8_t *data;              /* Buffer used to store the sent segment (header + seg_value + hash) */
            uint16_t length;            /* Length of the sent segment */
            uint8_t  expect_pdu;        /* Next expected pdu of the device */
            uint8_t  late_dev_count;    /* Devices connected after update_ready is true. Device connected after ota start, to use send ota after current job completed */
        } send; /* Role of the device which sends OTA segments (self can only be SoftAP) */
    };
    /* Callback used to post message to WiFi queue (currently) */
    esp_err_t (*ota_bearer_post_cb)(ble_mesh_ota_msg_t *msg, uint32_t timeout, bool to_front);
    /* Callback used to notify the completion of OTA procedure */
    esp_err_t (*ota_complete_cb)(uint8_t ota_op);
    /* Callback used to notify the fail of OTA procedure */
    esp_err_t (*ota_fail_cb)(uint8_t ota_op);
} g_ota_ctx;

static QueueHandle_t g_ota_queue;

#define OTA_SEG_ACK_ARRAY_SIZE() \
        ((g_ota_ctx.max_seg_seq + 1) % 8 ? \
        (g_ota_ctx.max_seg_seq + 1) / 8 + 1 : (g_ota_ctx.max_seg_seq + 1) / 8)

#define IS_OTA_SEG_ACKED(seg_seq) \
        (g_ota_ctx.recv.seg_ack_array[(seg_seq) / 8] & BIT((seg_seq) % 8) ? true : false)

#define SET_OTA_SEG_ACKED(seg_seq) \
        (g_ota_ctx.recv.seg_ack_array[(seg_seq) / 8] |= BIT((seg_seq) % 8))

#define CLEAR_OTA_SEG_ACKED(seg_seq) \
        (g_ota_ctx.recv.seg_ack_array[(seg_seq) / 8] &= ~BIT((seg_seq) % 8))

static esp_timer_handle_t g_ota_timer;

uint8_t ble_mesh_get_ota_dev_count(void)
{
    ENTER_FUNC();
    return g_ota_ctx.send.ota_dev_count;
}

uint16_t ble_mesh_get_ota_seg_count(void)
{
    ENTER_FUNC();
    /* This shall be called after ble_mesh_ota_op_init() */
    return (g_ota_ctx.max_seg_seq + 1);
}

static bool is_all_ota_seg_recv(void)
{
    ENTER_FUNC();
    if (g_ota_ctx.max_seg_seq == BLE_MESH_OTA_INVALID_SEG_SEQ) {
        return false;
    }

    for (int i = 0; i <= g_ota_ctx.max_seg_seq; i++) {
        if (!IS_OTA_SEG_ACKED(i)) {
            return false;
        }
    }

    return true;
}

esp_err_t ble_mesh_store_send_ota_seg_device(const uint8_t addr[6])
{
    ENTER_FUNC();
    if (!addr) {
        ESP_LOGE(TAG, "Invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    if (!memcmp(g_ota_ctx.recv.addr, addr, BLE_MESH_OTA_DEV_ADDR_LEN)) {
        ESP_LOGW(TAG, "Reconnect with sending OTA segment device "MACSTR"", MAC2STR(addr));
    } else {
        memcpy(g_ota_ctx.recv.addr, addr, BLE_MESH_OTA_DEV_ADDR_LEN);
    }

    return ESP_OK;
}

static struct ota_device* get_ota_device(const uint8_t addr[6])
{
    ENTER_FUNC();
    if (!addr) {
        ESP_LOGE(TAG, "Invalid argument");
        return NULL;
    }

    for (int i = 0; i < g_ota_ctx.send.max_device; i++) {
        struct ota_device *device = &g_ota_ctx.send.devices[i];
        if (!memcmp(device->addr, addr, BLE_MESH_OTA_DEV_ADDR_LEN)) {
            return device;
        }
    }

    ESP_LOGE(TAG, "Failed to get ota device "MACSTR"", MAC2STR(addr));

    return NULL;
}

esp_err_t ble_mesh_reset_ota_device(const uint8_t addr[6])
{
    ENTER_FUNC();
    if (!addr) {
        g_ota_ctx.max_seg_seq = BLE_MESH_OTA_INVALID_SEG_SEQ;
        g_ota_ctx.recv.last_seg_recv = BLE_MESH_OTA_INVALID_SEG_SEQ;
        g_ota_ctx.recv.seg_valid_count = 0;
        if (g_ota_ctx.recv.seg_ack_array) {
            free(g_ota_ctx.recv.seg_ack_array);
            g_ota_ctx.recv.seg_ack_array = NULL;
        }
        g_ota_ctx.recv.first_time_recv = true;
    } else {
        struct ota_device *device = get_ota_device(addr);
        if (!device) {
            ESP_LOGE(TAG, "Failed to get ota device "MACSTR"", MAC2STR(addr));
            return ESP_FAIL;
        }
        device->last_seg = BLE_MESH_OTA_INVALID_SEG_SEQ;
        device->send_stop = false;
    }

    return ESP_OK;
}

/* APIs to send OTA segments and receive OTA pdu */

static esp_err_t prepare_ota_segment(uint16_t seg_seq)
{
    ENTER_FUNC();
    static uint16_t count = 0;
    uint8_t retry = 0;
    uint16_t seg_len = 0, total_len = 0;
    uint8_t  *buf    = NULL;
    uint32_t offset  = 0;
    uint32_t opcode  = 0;
    esp_err_t err;

    if ((seg_seq == g_ota_ctx.max_seg_seq) && (g_ota_ctx.total_bin_size % CONFIG_BLE_MESH_OTA_SEGMENT_SIZE)) {
        ESP_LOGI(TAG, "Preparing the last segment 0x%04x to be sent", seg_seq);
        seg_len = g_ota_ctx.total_bin_size % CONFIG_BLE_MESH_OTA_SEGMENT_SIZE;
    } else {
        seg_len = CONFIG_BLE_MESH_OTA_SEGMENT_SIZE;
    }

    ESP_LOGD(TAG, "malloc count: %d, %d", count++, __LINE__);
    while (retry++ < 10 && !(buf = malloc(BLE_MESH_OTA_SEG_HEADER_SIZE + CONFIG_BLE_MESH_OTA_SEGMENT_SIZE +
                 BLE_MESH_OTA_SEG_HASH_SIZE))) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGW(TAG, "malloc retry %d, %d", retry, __LINE__);
    }
    if (!buf) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        buf = malloc(BLE_MESH_OTA_SEG_HEADER_SIZE + CONFIG_BLE_MESH_OTA_SEGMENT_SIZE +
                    BLE_MESH_OTA_SEG_HASH_SIZE);
        if (!buf) {
            ESP_LOGE(TAG, "Failed to allocate memory, seg_seq 0x%04x", seg_seq);
            return ESP_FAIL;
        }
    }

    offset = seg_seq * CONFIG_BLE_MESH_OTA_SEGMENT_SIZE;
    opcode = BLE_MESH_OTA_SEGMENT_OPCODE;

    /* 4 octets opcode + 2 octets total seg count + 2 octets current seg seq + 1024 octets OTA segment */
    memcpy(buf, &opcode, sizeof(opcode));
    total_len += sizeof(opcode);

    memcpy(buf + total_len, &g_ota_ctx.max_seg_seq, sizeof(g_ota_ctx.max_seg_seq));
    total_len += sizeof(g_ota_ctx.max_seg_seq);

    memcpy(buf + total_len, &seg_seq, sizeof(seg_seq));
    total_len += sizeof(seg_seq);

    if (g_ota_ctx.ota_partition) {
        ESP_LOGD(TAG, "ble mesh partition read, offset: %d, total length: %d, seg length: %d, %d", offset, total_len, seg_len, __LINE__);
        err = esp_partition_read(g_ota_ctx.ota_partition, offset, buf + total_len, seg_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read partition, %d", __LINE__);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "ota partition is NULL");
    }
    total_len += seg_len;

    /* For last OTA segment, it may be smaller than CONFIG_BLE_MESH_OTA_SEGMENT_SIZE */
    if (CONFIG_BLE_MESH_OTA_SEGMENT_SIZE - seg_len) {
        memset(buf + total_len, 0xff, CONFIG_BLE_MESH_OTA_SEGMENT_SIZE - seg_len);
        total_len += CONFIG_BLE_MESH_OTA_SEGMENT_SIZE - seg_len;
    }

    g_ota_ctx.send.data = buf;
    g_ota_ctx.send.length = total_len;

    return ESP_OK;
}

static bool stop_send_ota_seg(void)
{
    ENTER_FUNC();
    for (int i = 0; i < g_ota_ctx.send.ota_dev_count; i++) {
        if (g_ota_ctx.send.devices[i].send_stop) {
            return true;
        }
    }

    return false;
}

static esp_err_t send_ota_segment(void)
{
    ENTER_FUNC();
    struct ota_device *device = NULL;
    ble_mesh_ota_msg_t msg = {0};
    const uint8_t zero[6] = {0};
    uint16_t seg_send;

    /* If any "send_stop" flag is true, stop sending OTA segments to the devices */
    if (stop_send_ota_seg()) {
        ESP_LOGW(TAG, "Stop sending OTA segments");
        return ESP_OK;
    }

    if (g_ota_ctx.send.last_seg_sent != BLE_MESH_OTA_INVALID_SEG_SEQ &&
        g_ota_ctx.send.last_seg_sent >= g_ota_ctx.max_seg_seq) {
        return ESP_OK;
    }

    seg_send = (g_ota_ctx.send.last_seg_sent == BLE_MESH_OTA_INVALID_SEG_SEQ) ? 0 : g_ota_ctx.send.last_seg_sent + 1;

    if (prepare_ota_segment(seg_send) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to prepare OTA segment %d", seg_send);
        return ESP_FAIL;
    }

    /* Starts posting OTA segments to sending bearer (e.g. TCP) */
    msg.flag   = BLE_MESH_OTA_SEG_SEND;
    msg.data   = g_ota_ctx.send.data;
    msg.length = g_ota_ctx.send.length;

    /* 4 octets opcode + 2 octets total seg count + 2 octets current seg seq + 1024 octets OTA segment */
    ESP_LOGI(TAG, "Current send seq: %d, total: %d", msg.data[7] << 8 | msg.data[6], msg.data[5] << 8 | msg.data[4]);

    for (int i = 0; i < g_ota_ctx.send.ota_dev_count; i++) {
        device = &g_ota_ctx.send.devices[i];
        if (memcmp(device->addr, zero, BLE_MESH_OTA_DEV_ADDR_LEN)) {
            memcpy(msg.addr, device->addr, BLE_MESH_OTA_DEV_ADDR_LEN);
            if (g_ota_ctx.ota_bearer_post_cb) {
                if (g_ota_ctx.ota_bearer_post_cb(&msg, portMAX_DELAY, false) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to post OTA_SEG_SEND event");
                    continue;
                }
                device->last_seg = seg_send;
            } else {
                ESP_LOGE(TAG, "ota_bearer_post_cb not set");
            }
        } else {
            ESP_LOGE(TAG, "device address is zero");
        }
    }

    g_ota_ctx.send.last_seg_sent = seg_send;

    if (g_ota_ctx.send.last_seg_sent == g_ota_ctx.max_seg_seq) {
        ESP_LOGI(TAG, "OTA segments sent to all the devices, start receiving OTA ACK...");
        g_ota_ctx.send.expect_pdu = OTA_ACK_PDU;
        msg.flag                  = BLE_MESH_OTA_PDU_RECV;
        for (int i = 0; i < g_ota_ctx.send.ota_dev_count; i++) {
            device = &g_ota_ctx.send.devices[i];
            if (memcmp(device->addr, zero, BLE_MESH_OTA_DEV_ADDR_LEN)) {
                memcpy(msg.addr, device->addr, BLE_MESH_OTA_DEV_ADDR_LEN);
                if (g_ota_ctx.ota_bearer_post_cb) {
                    if (g_ota_ctx.ota_bearer_post_cb(&msg, portMAX_DELAY, false) != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to post OTA_PDU_RECV event");
                        continue;
                    }
                } else {
                    ESP_LOGE(TAG, "ota_bearer_post_cb not set");
                }
            } else {
                ESP_LOGE(TAG, "device address is zero");
            }
        }
        return ESP_OK;
    }

    /* When OTA segments are posted, post an event to OTA queue to send other segments */
    msg.flag = BLE_MESH_OTA_SEG_SEND;
    if (ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post OTA_SEG_SEND event");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t ota_ack_pdu(struct ota_device *device, const uint8_t *data, uint16_t length)
{
    ENTER_FUNC();
    static uint8_t ack_recv_count;

    ESP_LOGI(TAG, "OTA ACK from device "MACSTR"", MAC2STR(device->addr));
    ESP_LOG_BUFFER_HEX(TAG, data, length);

    /* TODO:
     * 1. Check if all the segments are received by the OTA device, and resend
     *    the unreceived segments if necessary.
     * 2. If all the segments are received, may use a callback to notify the
     *    bearer layer the completion of the OTA procedure which can be used to
     *    close socket and disconnect with the OTA device for example.
     */

    memset(device->addr, 0, BLE_MESH_OTA_DEV_ADDR_LEN);
    device->last_seg  = BLE_MESH_OTA_INVALID_SEG_SEQ;
    device->send_stop = false;

    if (++ack_recv_count == g_ota_ctx.send.ota_dev_count) {
        ESP_LOGI(TAG, "OTA ACK of all %d devices have been received", g_ota_ctx.send.ota_dev_count);
        g_ota_ctx.send.last_seg_sent = BLE_MESH_OTA_INVALID_SEG_SEQ;
        g_ota_ctx.send.expect_pdu    = OTA_PDU_MAX;
        ack_recv_count               = 0;
        // ota other device after current ota running
        if (g_ota_ctx.send.late_dev_count) {
            /* In case one or more devices are connected after update_ready is true */
            for (int i = 0; i < g_ota_ctx.send.late_dev_count; i++) {
                uint8_t cp = g_ota_ctx.send.ota_dev_count + i;
                memcpy(&g_ota_ctx.send.devices[i], &g_ota_ctx.send.devices[cp], sizeof(struct ota_device));
            }
            g_ota_ctx.send.ota_dev_count  = g_ota_ctx.send.late_dev_count;
            g_ota_ctx.send.late_dev_count = 0;
            ble_mesh_ota_msg_t msg = {
                .flag = BLE_MESH_OTA_SEG_SEND,
            };
            if (ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to post OTA_SEG_SEND event");
                return ESP_FAIL;
            }
            return ESP_OK;
        }
        /* If finish updating "ota_dev_count" devices, we will not restart immediately
         * and will wait a few more seconds to check if we can get other need-ota device(s). */
        g_ota_ctx.send.ota_dev_count = 0;
        g_ota_ctx.send.update_ready = false;

        /* When all devices have been updated successfully, notify wifi task to clear all
         * station information and call ota_complete_cb. */
        ble_mesh_ota_msg_t msg = {
            .flag = BLE_MESH_OTA_PROC_DONE,
            .length = g_ota_ctx.total_bin_size,
        };
        if (g_ota_ctx.ota_bearer_post_cb) {
            if (g_ota_ctx.ota_bearer_post_cb(&msg, portMAX_DELAY, false) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to post OTA_PROC_DONE event");
                return ESP_FAIL;
            }
        }
        if (g_ota_ctx.ota_complete_cb) {
            g_ota_ctx.ota_complete_cb(g_ota_ctx.ota_op);
        }
    }

    return ESP_OK;
}

static const struct {
    esp_err_t (*func)(struct ota_device *device, const uint8_t *data, uint16_t length);
    uint8_t rfu;
} ota_msg_handler[] = {
    { ota_ack_pdu, 0 },
};

static esp_err_t recv_ota_pdu(const uint8_t addr[6], const uint8_t *data, uint16_t length)
{
    ENTER_FUNC();
    struct ota_device *device = NULL;
    uint32_t opcode;

    if (!addr || !data) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    if (length <= BLE_MESH_OTA_OPCODE_LEN) {
        ESP_LOGE(TAG, "Too small OTA pdu length %d", length);
        return ESP_FAIL;
    }

    device = get_ota_device(addr);
    if (!device) {
        ESP_LOGE(TAG, "Failed to get OTA device "MACSTR"", MAC2STR(addr));
        return ESP_FAIL;
    }

    opcode = data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0];
    switch (opcode) {
    case BLE_MESH_OTA_ACK_PDU_OPCODE:
        break;
    default:
        ESP_LOGE(TAG, "Invalid OTA pdu opcode 0x%08x", opcode);
        return ESP_FAIL;
    }

    if (g_ota_ctx.send.expect_pdu != (opcode & 0xff)) {
        ESP_LOGE(TAG, "Unexpected OTA pdu opcode 0x%08x", opcode);
        return ESP_OK;
    }

    return ota_msg_handler[(opcode & 0xff) - 1].func(device, data + 4, length - 4);
}

/* APIs to send OTA PDUs and receive OTA segments */

static esp_err_t send_ota_pdu(uint32_t opcode)
{
    ENTER_FUNC();
    uint16_t length = BLE_MESH_OTA_OPCODE_LEN;
    uint8_t *pdu = NULL;

    switch (opcode) {
    case BLE_MESH_OTA_ACK_PDU_OPCODE:
        if (!g_ota_ctx.recv.seg_ack_array) {
            ESP_LOGE(TAG, "OTA segments ack array is NULL");
            return ESP_FAIL;
        }
        length += OTA_SEG_ACK_ARRAY_SIZE();
        pdu = calloc(1, length);
        if (!pdu) {
            ESP_LOGE(TAG, "Failed to allocate memory, %d", __LINE__);
            return ESP_ERR_NO_MEM;
        }
        memcpy(pdu, &opcode, sizeof(opcode));
        memcpy(pdu + sizeof(opcode), g_ota_ctx.recv.seg_ack_array, OTA_SEG_ACK_ARRAY_SIZE());
        break;
    default:
        ESP_LOGE(TAG, "Unknown OTA pdu 0x%08x to send", opcode);
        return ESP_ERR_INVALID_ARG;
    }

    ble_mesh_ota_msg_t msg = {
        .flag = BLE_MESH_OTA_PDU_SEND,
        .data = pdu,
        .length = length,
    };
    memcpy(msg.addr, g_ota_ctx.recv.addr, BLE_MESH_OTA_DEV_ADDR_LEN);
    if (g_ota_ctx.ota_bearer_post_cb) {
        if (g_ota_ctx.ota_bearer_post_cb(&msg, portMAX_DELAY, false) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post OTA_PDU_SEND event");
            return ESP_FAIL;
        }
    }

    if (opcode == BLE_MESH_OTA_ACK_PDU_OPCODE) {
        /* Use the first_time_recv flag in case some segments are received again */
        g_ota_ctx.recv.first_time_recv = false;
    }

    return ESP_OK;
}

static esp_err_t store_ota_segment(const uint8_t *seg, uint16_t len, uint16_t seg_seq)
{
    ENTER_FUNC();
    static int64_t seg0_time;
    size_t seg_len;
    esp_err_t err;

    /* Three error situations:
     * 1. invalid length
     * 2. fail to calculate hash value
     * 3. invalid hash value
     */
    if (len <= BLE_MESH_OTA_SEG_HASH_SIZE) {
        ESP_LOGW(TAG, "Too small (seg + hash) length %d", len);
        return ESP_FAIL;
    }

    seg_len = len - BLE_MESH_OTA_SEG_HASH_SIZE;

    /* This is just used for debug */
    if (seg_seq == 0 || seg_seq == g_ota_ctx.max_seg_seq) {
        if (seg_seq == 0) {
            seg0_time = esp_timer_get_time();
            ESP_LOGW(TAG, "Receiving OTA segment 0 at %lld us", seg0_time);
        } else {
            ESP_LOGI(TAG, "Receiving segment %d successfully", seg_seq);
        }
    }

    err = esp_ota_write(g_ota_ctx.ota_handle, seg, seg_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write ota data, seg_seq %d", seg_seq);
        return ESP_FAIL;
    }

    /* Updates the received OTA segments information */
    SET_OTA_SEG_ACKED(seg_seq);
    g_ota_ctx.total_bin_size += seg_len;
    g_ota_ctx.recv.seg_valid_count++;

    return ESP_OK;
}

static esp_err_t recv_ota_segment(const uint8_t *data, uint16_t length)
{
    ENTER_FUNC();
    uint16_t seg_seq = BLE_MESH_OTA_INVALID_SEG_SEQ;
    static uint16_t seg_recv_count;
    esp_err_t err = ESP_OK;

    if (!data) {
        ESP_LOGE(TAG, "Invalid ota segment");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_ota_ctx.recv.seg_valid_count == g_ota_ctx.max_seg_seq + 1) {
        ESP_LOGI(TAG, "All OTA segments have been received");
        return ESP_OK;
    }

    if (length <= BLE_MESH_OTA_SEG_HEADER_SIZE + BLE_MESH_OTA_SEG_HASH_SIZE) {
        ESP_LOGE(TAG, "Invalid OTA segment length %d", length);
        err = ESP_FAIL;
        goto end;
    }

    uint32_t opcode = data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0];
    if (opcode != BLE_MESH_OTA_SEGMENT_OPCODE) {
        ESP_LOGE(TAG, "Invalid OTA segment opcode 0x%08x", opcode);
        err = ESP_FAIL;
        goto end;
    }

    /* 4 octets opcode + 2 octets total seg count + 2 octets current seg seq + 1024 octets OTA segment */
    ESP_LOGI(TAG, "Current recv seq: %d, total: %d", data[7] << 8 | data[6], data[5] << 8 | data[4]);

    data   += sizeof(opcode);
    length -= sizeof(opcode);

    uint16_t max_seq = data[1] << 8 | data[0];
    if (g_ota_ctx.max_seg_seq == BLE_MESH_OTA_INVALID_SEG_SEQ) {
        g_ota_ctx.max_seg_seq = max_seq;
        /* When maximum sequence number is decided, allocate memory for seg_ack_array */
        g_ota_ctx.recv.seg_ack_array = calloc(1, OTA_SEG_ACK_ARRAY_SIZE());
        if (!g_ota_ctx.recv.seg_ack_array) {
            ESP_LOGE(TAG, "Failed to allocate memory, %d", __LINE__);
            err = ESP_FAIL;
            goto end;
        }
    } else {
        if (g_ota_ctx.max_seg_seq != max_seq) {
            ESP_LOGE(TAG, "Recv max_seq 0x%04x diff with stored max_seq 0x%04x", max_seq, g_ota_ctx.max_seg_seq);
            err = ESP_FAIL;
            goto end;
        }
    }

    data   += sizeof(max_seq);
    length -= sizeof(max_seq);

    seg_seq = data[1] << 8 | data[0];
    if (seg_seq > max_seq) {
        ESP_LOGE(TAG, "Too big seg_seq 0x%04x", seg_seq);
        err = ESP_FAIL;
        goto end;
    }

    data += sizeof(seg_seq);
    length -= sizeof(seg_seq);

    /* Check if the segment has already been received. When the OTA segment is received
     * successfully in the first time, the corresponding bit will be set in seg_ack_array.
     * This check is used for segments received after OTA ACK FINISH is sent. 
     */
    if (IS_OTA_SEG_ACKED(seg_seq)) {
        ESP_LOGI(TAG, "OTA segment 0x%04x has already been received", seg_seq);
        return ESP_OK;
    }

    err = store_ota_segment(data, length, seg_seq);

end:
    if (g_ota_ctx.recv.first_time_recv) {
        if (seg_seq != BLE_MESH_OTA_INVALID_SEG_SEQ) {
            g_ota_ctx.recv.last_seg_recv = seg_seq;
        }
        seg_recv_count++;
    }

    /* Check if it is the time to send OTA ACK pdu.
     * 1. All OTA segments are received successfully;
     * 2. All segments are received (no matter valid or invalid);
     * 3. Last received segment is max_seg_seq.
     */
    if (g_ota_ctx.max_seg_seq != BLE_MESH_OTA_INVALID_SEG_SEQ) {
        if ((g_ota_ctx.recv.first_time_recv &&
            ((seg_recv_count == g_ota_ctx.max_seg_seq + 1) ||
            (g_ota_ctx.recv.last_seg_recv == g_ota_ctx.max_seg_seq))) ||
            (g_ota_ctx.recv.seg_valid_count == g_ota_ctx.max_seg_seq + 1)) {
            ESP_LOGI(TAG, "Send OTA ACK");
            if (send_ota_pdu(BLE_MESH_OTA_ACK_PDU_OPCODE) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send OTA ACK pdu");
                return ESP_FAIL;
            }
            if (!is_all_ota_seg_recv()) {
                /* TODO: Check if all OTA segments are received and if not, tcp continues to receive */
            }
        }
    }

    if (is_all_ota_seg_recv()) {
        ESP_LOGI(TAG, "All OTA segments have been received");
        err = esp_ota_end(g_ota_ctx.ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to end ota (err %d)", err);
            return ESP_FAIL;
        }
    }

    return err;
}

esp_err_t ble_mesh_ota_op_task_post(ble_mesh_ota_msg_t *msg, uint32_t timeout, bool to_front)
{
    ENTER_FUNC();
    BaseType_t ret;

    if (to_front) {
        ret = xQueueSendToFront(g_ota_queue, msg, timeout);
    } else {
        ret = xQueueSend(g_ota_queue, msg, timeout);
    }

    return (ret == pdTRUE) ? ESP_OK : ESP_FAIL;
}

static void ota_timer_cb(void *arg);
static bool g_ota_timer_start;

static void start_ota_timer(void)
{
    ENTER_FUNC();
    if (g_ota_timer_start == false) {
        esp_timer_create_args_t args = {
            .callback = &ota_timer_cb,
            .name     = "g_ota_timer",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &g_ota_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(g_ota_timer, START_OTA_SEND_TIMEOUT_US));
        /* Start send firmware When some device connected this device within 30s */
        g_ota_timer_start = true;
    }
}

static void stop_ota_timer(void)
{
    ENTER_FUNC();
    if (g_ota_timer_start == true) {
        ESP_LOGI(TAG, "Stop Send OTA timer, Start send firmware");
        esp_timer_stop(g_ota_timer);
        esp_timer_delete(g_ota_timer);
        g_ota_timer_start = false;
    }
}

static void ota_timer_cb(void *arg)
{
    ENTER_FUNC();
    ble_mesh_ota_msg_t msg = {0};
    static uint8_t prev_count = 0;

    ESP_LOGI(TAG, "current time %lld us", esp_timer_get_time());

    if (prev_count == g_ota_ctx.send.ota_dev_count) {
        /* Stops OTA timer and posts event to start sending OTA segments */
        ESP_LOGI(TAG, "OTA device count is %d", g_ota_ctx.send.ota_dev_count);
        stop_ota_timer();
        msg.flag = BLE_MESH_OTA_SEG_SEND;
        if (ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post OTA_SEG_SEND event");
            return;
        }

        g_ota_ctx.send.update_ready = true;
        return;
    }

    prev_count = g_ota_ctx.send.ota_dev_count;
}

static esp_err_t store_ota_device(const uint8_t addr[6])
{
    ENTER_FUNC();
    struct ota_device *device = NULL;
    const uint8_t zero[6] = {0};

    if (!addr) {
        ESP_LOGE(TAG, "Invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < g_ota_ctx.send.max_device; i++) {
        device = &g_ota_ctx.send.devices[i];
        if (!memcmp(device->addr, addr, BLE_MESH_OTA_DEV_ADDR_LEN)) {
            ESP_LOGW(TAG, "Reconnect with receiving OTA segment device "MACSTR"", MAC2STR(addr));
            device->last_seg = BLE_MESH_OTA_INVALID_SEG_SEQ;
            return ESP_OK;
        }
    }

    for (int i = 0; i < g_ota_ctx.send.max_device; i++) {
        device = &g_ota_ctx.send.devices[i];
        if (!memcmp(device->addr, zero, BLE_MESH_OTA_DEV_ADDR_LEN)) {
            memcpy(device->addr, addr, BLE_MESH_OTA_DEV_ADDR_LEN);
            device->last_seg = BLE_MESH_OTA_INVALID_SEG_SEQ;
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "OTA device queue is full");

    return ESP_FAIL;
}

esp_err_t ble_mesh_store_recv_ota_seg_device(const uint8_t addr[6])
{
    ENTER_FUNC();
    ble_mesh_ota_msg_t msg = {0};

    if (!addr) {
        ESP_LOGE(TAG, "Invalid device address");
        return ESP_ERR_ESPNOW_ARG;
    }

    ESP_LOGI(TAG, "current time %lld us, update_ready is %s", esp_timer_get_time(), g_ota_ctx.send.update_ready ? "true" : "false");

    if (store_ota_device(addr) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store OTA device address");
        return ESP_FAIL;
    }

    if (g_ota_ctx.send.update_ready == false) {
        if (g_ota_ctx.ota_op == BLE_MESH_OTA_OP_SEG_SEND) {
            ESP_LOGD(TAG, "ReStart Send OTA timer");
            /* In case get one or more devices after self-OTA is done. */
            start_ota_timer();
        }

        if (++g_ota_ctx.send.ota_dev_count >= g_ota_ctx.send.max_device) {
            /* If ota_dev_count reaches max_device, stop timer and start sending segments */
            ESP_LOGI(TAG, "OTA device count is %d", g_ota_ctx.send.ota_dev_count);
            if (g_ota_ctx.ota_op == BLE_MESH_OTA_OP_SEG_SEND) {
                stop_ota_timer();
                msg.flag = BLE_MESH_OTA_SEG_SEND;
                if (ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to post OTA_SEG_SEND event");
                    return ESP_FAIL;
                }
            }
            g_ota_ctx.send.update_ready = true;
        }
    } else {
        g_ota_ctx.send.late_dev_count++;
        ESP_LOGI(TAG, "late_dev_count %d", g_ota_ctx.send.late_dev_count);
    }

    return ESP_OK;
}

/**
 * post event to wifi tast to 
*/
esp_err_t ble_mesh_ota_op_init_post(ble_mesh_ota_op_init_t *init)
{
    ENTER_FUNC();
    ble_mesh_ota_task_init();

    ble_mesh_ota_msg_t msg = {0};

    msg.flag = BLE_MESH_OTA_INIT;
    msg.data = malloc(sizeof(ble_mesh_ota_op_init_t));
    memcpy(msg.data, init, sizeof(ble_mesh_ota_op_init_t));

    return ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);
}

esp_err_t ble_mesh_ota_op_init(ble_mesh_ota_op_init_t *init)
{
    ENTER_FUNC();
    esp_err_t err = ESP_OK;

    if (!init || !init->ota_bearer_post_cb || !init->ota_complete_cb ||
        (init->ota_op == BLE_MESH_OTA_OP_SEG_SEND && init->max_dev == 0)) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    g_ota_ctx.ota_op = init->ota_op;

    switch (init->ota_op) {
    case BLE_MESH_OTA_OP_SEG_RECV:
        ESP_LOGI(TAG, "init BLE_MESH_OTA_OP_SEG_RECV, %d", __LINE__);
        g_ota_ctx.total_bin_size       = 0;
        g_ota_ctx.max_seg_seq          = BLE_MESH_OTA_INVALID_SEG_SEQ;
        g_ota_ctx.recv.last_seg_recv   = BLE_MESH_OTA_INVALID_SEG_SEQ;
        g_ota_ctx.recv.seg_valid_count = 0;
        g_ota_ctx.recv.first_time_recv = true;
        g_ota_ctx.ota_partition        = esp_ota_get_next_update_partition(NULL);
        memset(g_ota_ctx.recv.addr, 0, BLE_MESH_OTA_DEV_ADDR_LEN);
        if (!g_ota_ctx.ota_partition) {
            ESP_LOGE(TAG, "Failed to get next ota partition");
            return ESP_FAIL;
        }

        ESP_LOGD(TAG, "ble mesh ota begin start");
        /* The specified OTA partition is erased to the specified OTA image size. */
        err = esp_ota_begin(g_ota_ctx.ota_partition, OTA_SIZE_UNKNOWN, &g_ota_ctx.ota_handle);
        ESP_LOGD(TAG, "ble mesh ota begin stop");
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to begin ota");
            return err;
        }
        break;
    case BLE_MESH_OTA_OP_SEG_SEND:
        ESP_LOGI(TAG, "init BLE_MESH_OTA_OP_SEG_SEND, %d", __LINE__);
        // g_ota_ctx.total_bin_size = init->total_bin_size;
        if (g_ota_ctx.total_bin_size % CONFIG_BLE_MESH_OTA_SEGMENT_SIZE) {
            g_ota_ctx.max_seg_seq = g_ota_ctx.total_bin_size / CONFIG_BLE_MESH_OTA_SEGMENT_SIZE;
        } else {
            g_ota_ctx.max_seg_seq = g_ota_ctx.total_bin_size / CONFIG_BLE_MESH_OTA_SEGMENT_SIZE - 1;
        }
        g_ota_ctx.send.max_device = init->max_dev;
        for (uint8_t i = 0; i < g_ota_ctx.send.max_device; i++) {
            struct ota_device *device = &g_ota_ctx.send.devices[i];
            memset(device->addr, 0, BLE_MESH_OTA_DEV_ADDR_LEN);
            device->last_seg  = BLE_MESH_OTA_INVALID_SEG_SEQ;
            device->send_stop = false;
        }
        g_ota_ctx.send.ota_dev_count = 0;
        g_ota_ctx.send.update_ready  = false;
        g_ota_ctx.send.last_seg_sent = BLE_MESH_OTA_INVALID_SEG_SEQ;
        g_ota_ctx.send.expect_pdu    = OTA_PDU_MAX;
        g_ota_ctx.ota_partition      = init->ota_partition;
        ESP_LOGI(TAG, "total bin size: %d, segment count: %d", g_ota_ctx.total_bin_size, g_ota_ctx.max_seg_seq);
        break;
    default:
        ESP_LOGD(TAG, "Unknown OTA operation 0x%02x", init->ota_op);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "op init callback, %d", __LINE__);
    g_ota_ctx.ota_fail_cb        = init->ota_fail_cb;
    g_ota_ctx.ota_complete_cb    = init->ota_complete_cb;
    g_ota_ctx.ota_bearer_post_cb = init->ota_bearer_post_cb;

    ble_mesh_ota_task_init();

    return ESP_OK;
}

static void ble_mesh_ota_op_task(void *param)
{
    ENTER_FUNC();
    static uint16_t count = 0;
    ble_mesh_ota_msg_t msg = {0};

    while(1) {
        if (xQueueReceive(g_ota_queue, &msg, portMAX_DELAY)) {
            switch (msg.flag) {
            case BLE_MESH_OTA_SEG_SEND:
                ESP_LOGD(TAG, "SEG_SEND, %d, free heap: %u", __LINE__, esp_get_free_heap_size());
                send_ota_segment();
                break;
            case BLE_MESH_OTA_PDU_RECV:
                ESP_LOGD(TAG, "PDU_RECV, %d, free heap: %u", __LINE__, esp_get_free_heap_size());
                recv_ota_pdu(msg.addr, msg.data, msg.length);
                /* msg.ctx is allocated by receiving bearer (i.e. TCP) to store OTA pdu, need to free it here. */
                if (msg.data) {
                    free(msg.data);
                }
                break;
            case BLE_MESH_OTA_SEG_RECV:
                ESP_LOGD(TAG, "SEG_RECV, %d, free heap: %u, %d, %d", __LINE__, esp_get_free_heap_size(), msg.data[5] << 8 | msg.data[4], msg.data[7] << 8 | msg.data[6]);
                recv_ota_segment(msg.data, msg.length);
                /* msg.ctx is allocated by receiving bearer (i.e. TCP) to store OTA segment, need to free it here. */
                if (msg.data) {
                    ESP_LOGD(TAG, "free count: %d", count++);
                    free(msg.data);
                }
                break;
            case BLE_MESH_OTA_FAIL:
                ESP_LOGD(TAG, "BLE MESH OTA FAIL, %d", __LINE__);
                if (g_ota_ctx.ota_fail_cb) {
                    g_ota_ctx.ota_fail_cb(g_ota_ctx.ota_op);
                }
                break;
            case BLE_MESH_OTA_PROC_DONE:
            case BLE_MESH_OTA_DOWN_URL_DONE:
                ESP_LOGD(TAG, "BLE MESH OTA DONE, %d", __LINE__);
                if (g_ota_ctx.ota_complete_cb) {
                    g_ota_ctx.ota_complete_cb(g_ota_ctx.ota_op);
                }
                if (g_ota_ctx.ota_op == BLE_MESH_OTA_OP_SEG_RECV) {
                    if (msg.flag == BLE_MESH_OTA_DOWN_URL_DONE) {
                        g_ota_ctx.total_bin_size = msg.length;
                        ESP_LOGI(TAG, "ble mesh ota process done, firmware length: %d", msg.length);
                    }
                    /* OTA update is completed, now change to OTA Send role */
                    g_ota_ctx.ota_op = BLE_MESH_OTA_OP_SEG_SEND;
                    /* max_seg_seq & total_bin_size have already been updated */
                }
                break;
            case BLE_MESH_OTA_SEG_SEND_STOP: {
                ESP_LOGD(TAG, "BLE MESH OTA SEND STOP, %d", __LINE__);
                struct ota_device *device = get_ota_device(msg.addr);
                if (!device) {
                    ESP_LOGE(TAG, "Failed to get OTA device "MACSTR"", MAC2STR(msg.addr));
                    break;
                }
                device->send_stop = true;
                /* Use BLE_MESH_OTA_INVALID_SEG_SEQ instead of "(msg.data[1] << 8 | msg.data[0]) - 1" here
                * because esp_ota_write() needs to check if the very first octet of the OTA bin is 0xE9.
                * TODO: Use "(msg.data[1] << 8 | msg.data[0]) - 1" cause we add a macro "USE_ESP_OTA_API".
                */
#if 0
                g_ota_ctx.send.last_seg_sent = (msg.data[1] << 8 | msg.data[0]) - 1;
#else
                g_ota_ctx.send.last_seg_sent = BLE_MESH_OTA_INVALID_SEG_SEQ;
#endif
                if (msg.data) {
                    free(msg.data);
                }
                ESP_LOGD(TAG, "Stop sending to device "MACSTR"", MAC2STR(msg.addr));
                break;
            }
            case BLE_MESH_OTA_SEG_SEND_CONT: {
                ESP_LOGD(TAG, "BLE_MESH_OTA_SEG_SEND_CONT, %d", __LINE__);
                struct ota_device *device = get_ota_device(msg.addr);
                if (!device) {
                    ESP_LOGE(TAG, "Failed to get OTA device "MACSTR"", MAC2STR(msg.addr));
                    break;
                }
                ESP_LOGD(TAG, "Resume sending to device "MACSTR"", MAC2STR(msg.addr));
                device->send_stop = false;
                if (!stop_send_ota_seg()) {
                    /* Use this event to notify Wi-Fi task to clear some flag */
                    msg.flag = BLE_MESH_OTA_SEG_SEND_CONT;
                    if (g_ota_ctx.ota_bearer_post_cb) {
                        if (g_ota_ctx.ota_bearer_post_cb(&msg, portMAX_DELAY, false) != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to post OTA_SEG_SEND_CONT event");
                            break;
                        }
                    }
                    msg.flag = BLE_MESH_OTA_SEG_SEND;
                    ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);
                }
                break;
            }
            case BLE_MESH_OTA_INIT:
                ESP_LOGD(TAG, "BLE_MESH_OTA_INIT, %d", __LINE__);
                ble_mesh_ota_op_init((ble_mesh_ota_op_init_t *)msg.data);
                free(msg.data);
                break;
            case BLE_MESH_OTA_TASK_DELETE:
                ESP_LOGD(TAG, "BLE_MESH_OTA_TASK_DELETE, %d", __LINE__);
                vQueueDelete(g_ota_queue);
                vTaskDelete(NULL);
                break;
            default:
                ESP_LOGE(TAG, "Unknown OTA message flag 0x%02x", msg.flag);
                break;
            }
        }
    }

    ESP_LOGE(TAG, "Delete BLE Mesh OTA task");
    vTaskDelete(NULL);
}

esp_err_t ble_mesh_ota_task_init(void)
{
    ENTER_FUNC();
    esp_err_t ret;
    static bool ota_task_init = false;

    if (ota_task_init) {
        return -EALREADY;
    }

    g_ota_queue = xQueueCreate(20, sizeof(ble_mesh_ota_msg_t));
    assert(g_ota_queue);

    ret = xTaskCreatePinnedToCore(ble_mesh_ota_op_task, "ota_op_task", 4096, NULL, 4, NULL, 1);
    assert(ret == pdTRUE);

    ota_task_init = true;

    return ESP_OK;
}

esp_err_t ble_mesh_ota_task_deinit(void)
{
    ENTER_FUNC();
    ble_mesh_ota_msg_t msg ={0};
    msg.flag = BLE_MESH_OTA_TASK_DELETE;

    ble_mesh_ota_op_task_post(&msg, portMAX_DELAY, false);

    return ESP_OK;
}
