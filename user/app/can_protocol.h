#ifndef __CAN_PROTOCOL_H
#define __CAN_PROTOCOL_H

#include <stdint.h>
#include "can.h"

#define CAN_PROTOCOL_DEFAULT_NODE_ID (0x10U)

#define CAN_PROTOCOL_HOST_COMMAND_BASE_ID (0x100U)
#define CAN_PROTOCOL_MOTOR_ACK_BASE_ID    (0x180U)
#define CAN_PROTOCOL_MOTOR_REPORT_BASE_ID (0x200U)

typedef enum
{
    CAN_PROTOCOL_CMD_SET_MODE = 0x01,
    CAN_PROTOCOL_CMD_SET_CURRENT_TARGET = 0x02,
    CAN_PROTOCOL_CMD_SET_SPEED_TARGET = 0x03,
    CAN_PROTOCOL_CMD_SET_POSITION_TARGET = 0x04,
    CAN_PROTOCOL_CMD_SET_SPEED_PID_PARAM = 0x10,
    CAN_PROTOCOL_CMD_GET_SPEED_PID_PARAM = 0x11,
    CAN_PROTOCOL_CMD_SET_POSITION_PID_PARAM = 0x12,
    CAN_PROTOCOL_CMD_GET_POSITION_PID_PARAM = 0x13,
    CAN_PROTOCOL_CMD_SET_CAN_ID = 0x20,
    CAN_PROTOCOL_CMD_GET_CAN_ID = 0x21,
    CAN_PROTOCOL_CMD_ZERO_CALIBRATE = 0x30,
    CAN_PROTOCOL_CMD_GET_ZERO = 0x31,
    CAN_PROTOCOL_CMD_REPORT_POSITION = 0x80,
    CAN_PROTOCOL_CMD_REPORT_SPEED = 0x81
} can_protocol_cmd_e;

typedef enum
{
    CAN_PROTOCOL_STATUS_OK = 0x00,
    CAN_PROTOCOL_STATUS_INVALID_CMD = 0x01,
    CAN_PROTOCOL_STATUS_INVALID_PARAM = 0x02,
    CAN_PROTOCOL_STATUS_INVALID_MODE = 0x03,
    CAN_PROTOCOL_STATUS_CAN_ERROR = 0x04
} can_protocol_status_e;

typedef enum
{
    CAN_PROTOCOL_PID_PARAM_P = 0x00,
    CAN_PROTOCOL_PID_PARAM_I = 0x01,
    CAN_PROTOCOL_PID_PARAM_D = 0x02,
    CAN_PROTOCOL_PID_PARAM_I_ACC_MAX = 0x03,
    CAN_PROTOCOL_PID_PARAM_OUT_MAX = 0x04
} can_protocol_pid_param_e;

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
 * @brief 主动上报当前电机位置和速度。
 * @return void
 */
void can_protocol_report_motor_state(void);

/**
 * @brief 在主循环上下文中处理待执行的 CAN 命令。
 * @return void
 */
void can_protocol_process(void);

/**
 * @brief 获取当前协议层使用的节点 ID。
 * @return uint16_t 当前节点 ID。
 */
uint16_t can_protocol_get_node_id(void);

#endif
