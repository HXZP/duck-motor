#ifndef __CAN_PROTOCOL_H
#define __CAN_PROTOCOL_H

#include <stdint.h>
#include "can.h"


/**
 * @brief 初始化 CAN 协议层并配置过滤器。
 * @return void
 */
void can_protocol_init(void);

/**
 * @brief 处理一帧接收到的 CAN 协议数据。
 * @param rxframe 接收帧头。
 * @param rx_data 接收到的数据区指针。
 * @return void
 */
void CAN_protocol_analysis(CAN_RxHeaderTypeDef rxframe, uint8_t *rx_data);

/**
 * @brief 在主循环上下文中处理待执行的 CAN 命令。
 * @return void
 */
void can_protocol_process(void);

#endif
