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

#ifndef _BLE_MESH_OTA_COMMON_H_
#define _BLE_MESH_OTA_COMMON_H_

#include <stdint.h>
#include "esp_err.h"

#define CID_ESP 0x02E5

#define BLE_MESH_OTA_PARTITION_NAME     "ota_hash"

#define BLE_MESH_OTA_MAX_UPGRADE_DEV_NUM    1 // Currently in the upgrade state, the number of connected devices is 1.
#define BLE_MESH_OTA_MAX_UNUPGRADE_DEV_NUM  3 // The maximum number of devices that support OTA at the same time after OTA completed.

#define BLE_MESH_OTA_INVALID_SEG_SEQ    0xFFFF

#define BLE_MESH_OTA_OP_SEG_RECV        0x00
#define BLE_MESH_OTA_OP_SEG_SEND        0x01

/* packet type (1) + flag (1, BIT0 表示数据包是否加密) + company id (2) +
 * current seq (2) + total seq (2) + segment len (2) + segment (len) + crc (4)
 * 其中:
 * type 0 表示手机发送的 ota request;
 * type 1 表示设备回复的 ota response;
 * type 2 表示手机发送的 ota information, 包含当前 bin 的大小 (4 个字节) 以及 16 个字节的 md5 校验值;
 * type 3 表示设备回复的 ota confirm, 其中 2 个字节表示需要手机每次发送的 bin 长度 (除最后一个),
 *        total seq (设备根据 bin 大小以及每次发送的 bin 长度算出) 以及每个 bin ack bit (为了断点重传)
 *
 * a) 设备开启 SoftAP 功能 (不需要重启);
 * b) 手机连接上设备后, 发送 ota request, ota request 中会包含 RSA Public Key;
 * c) 设备收到 ota request 后, 随机生成 10 个字节, 并和 company id, bin id 以及版本号组成 IV;
 *    设备使用收到的 RSA Public Key 加密 IV 后, 将其放入 ota response, 同时 flag BIT0 置 1 发送给手机;
 * d) 手机收到加密的 IV 后使用 private key 进行解密, 获取 IV. 同时根据将当前 bin 的信息放入
 *    ota information 中发送给设备;
 * e) 设备收到 ota information 后回复手机 ota confirm;
 * f) 手机收到 ota confirm 后, 根据其中的信息 (e.g. 每个 segment 的大小) 开始发送 ota segment, 每个
 *    ota segment 会使用 AES CFB NoPadding 的算法进行加密;
 * g) 设备收到 ota segment 后先校验 crc, 校验成功后再使用 RSA Public Key 进行解密, 并将 ota segment
 *    写入 flash. crc 校验失败, 解密失败, 写入 flash 失败或者写入 flash 成功, 设备都会回复 ota confirm;
 * h) 手机收到 ota confirm 后, 根据其中 bin ack bit, 决定是重传当前 ota segment 还是发送下一个 ota segment.
 */
#define BLE_MESH_OTA_PDU_OTA_REQ        0x00
#define BLE_MESH_OTA_PDU_OTA_RSP        0x01
#define BLE_MESH_OTA_PDU_OTA_INFO       0x02
#define BLE_MESH_OTA_PDU_OTA_CONF       0x03

#define BLE_MESH_OTA_SEGMENT_OPCODE     (0x02E5 << 16 | 0xEB << 8 | 0x00)
#define BLE_MESH_OTA_ACK_PDU_OPCODE     (0x02E5 << 16 | 0xEB << 8 | 0x01)

#define BLE_MESH_OTA_DEV_ADDR_LEN       6
#define BLE_MESH_OTA_OPCODE_LEN         4

#define BLE_MESH_OTA_SEG_HEADER_SIZE    8   /* opcode + total_seq + curr_seq */

#define BLE_MESH_OTA_SEG_HASH_SIZE      0

enum {
    OTA_SEGMENT,
    OTA_ACK_PDU,
    OTA_PDU_MAX,
};

typedef struct {
    uint8_t  addr[6];
    uint8_t *data;
    uint16_t length;
} ble_mesh_ota_tx_ctx_t;

enum {
    BLE_MESH_OTA_AP_GOT_STA,
    BLE_MESH_OTA_STA_GOT_AP,
    BLE_MESH_OTA_SEG_SEND,
    BLE_MESH_OTA_PDU_RECV,
    BLE_MESH_OTA_SEG_RECV,
    BLE_MESH_OTA_PDU_SEND,
    BLE_MESH_OTA_PROC_DONE,
    BLE_MESH_OTA_DOWN_URL_DONE,
    BLE_MESH_OTA_SEG_SEND_STOP,
    BLE_MESH_OTA_SEG_SEND_CONT,
    BLE_MESH_OTA_WIFI_INIT,
    BLE_MESH_OTA_MESH_START,
    BLE_MESH_OTA_MESH_STOP,
    BLE_MESH_OTA_MESH_NBVN,
    BLE_MESH_OTA_WIFI_TASK_DELETE,
    BLE_MESH_OTA_TCP_TASK_DELETE,
    BLE_MESH_OTA_TASK_DELETE,
    BLE_MESH_OTA_INIT,
    BLE_MESH_OTA_FAIL,
};

typedef struct {
    uint8_t  flag;    /* Indicate the OTA segment/PDU is sending or receiving */
    uint8_t  addr[6]; /* OTA device address */
    uint8_t *data;
    uint32_t length; /* Use to transmit ota bin size */
} ble_mesh_ota_msg_t;

#endif /* _BLE_MESH_OTA_COMMON_H_ */
