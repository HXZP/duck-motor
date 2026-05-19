#ifndef __CAN_PROTOCOL_H
#define __CAN_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can.h"

#include <stdint.h>

#define CAN_PROTOCOL_DEFAULT_NODE_ID       (0x10u)
#define CAN_PROTOCOL_OTA_CONTROL_BASE_ID   (0x400u)
#define CAN_PROTOCOL_OTA_RESPONSE_BASE_ID  (0x500u)

/**
 * @brief 初始化 Boot CAN 协议层并配置 OTA 接收过滤器。
 * @return void
 */
void can_protocol_init(void);

/**
 * @brief 处理一帧接收到的 Boot CAN 协议数据。
 * @param rxframe 接收帧头。
 * @param rx_data 接收数据缓冲区。
 * @return void
 */
void CAN_protocol_analysis(CAN_RxHeaderTypeDef rxframe, uint8_t *rx_data);

/**
 * @brief 在主循环上下文中处理待执行的 CAN 命令。
 * @return void
 */
void can_protocol_process(void);

/**
 * @brief 发送 YMODEM 响应字节。
 * @param data 待发送数据缓冲区。
 * @param len 待发送长度，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int can_protocol_send_ota_response(const uint8_t *data, uint32_t len);

/**
 * @brief 获取当前 Boot CAN 节点 ID。
 * @return uint16_t 当前节点 ID，单位：无。
 */
uint16_t can_protocol_get_node_id(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_PROTOCOL_H */
