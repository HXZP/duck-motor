#ifndef __CAN_PROTOCOL_H
#define __CAN_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "can.h"
#include "common/user_info.h"

#define CAN_PROTOCOL_MANAGE_NODE_ID       (USER_INFO_MANAGE_CAN_ID)
#define CAN_PROTOCOL_DEFAULT_NODE_ID      (CAN_PROTOCOL_MANAGE_NODE_ID)

#define CAN_PROTOCOL_HOST_COMMAND_BASE_ID (0x100u)
#define CAN_PROTOCOL_MOTOR_ACK_BASE_ID    (0x180u)
#define CAN_PROTOCOL_MOTOR_REPORT_BASE_ID (0x200u)
#define CAN_PROTOCOL_OTA_CONTROL_BASE_ID  (0x400u)

/**
 * @brief App CAN 命令码。
 */
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
    CAN_PROTOCOL_CMD_GET_APP_VERSION = 0x22,
    CAN_PROTOCOL_CMD_SET_REPORT_CONFIG = 0x23,
    CAN_PROTOCOL_CMD_GET_REPORT_CONFIG = 0x24,
    CAN_PROTOCOL_CMD_RESET_CAN_CONFIG = 0x25,
    CAN_PROTOCOL_CMD_SET_FOC_CONFIG = 0x26,
    CAN_PROTOCOL_CMD_GET_FOC_CONFIG = 0x27,
    CAN_PROTOCOL_CMD_ZERO_CALIBRATE = 0x30,
    CAN_PROTOCOL_CMD_GET_ZERO = 0x31,
    CAN_PROTOCOL_CMD_ENTER_BOOT_OTA = 0x40,
    CAN_PROTOCOL_CMD_MANAGE_REPORT = 0x60,
    CAN_PROTOCOL_CMD_MANAGE_IDENTIFY = 0x61,
    CAN_PROTOCOL_CMD_MANAGE_SET_CAN_ID = 0x62
} can_protocol_cmd_e;

/**
 * @brief App CAN 应答状态码。
 */
typedef enum
{
    CAN_PROTOCOL_STATUS_OK = 0x00,
    CAN_PROTOCOL_STATUS_INVALID_CMD = 0x01,
    CAN_PROTOCOL_STATUS_INVALID_PARAM = 0x02,
    CAN_PROTOCOL_STATUS_INVALID_MODE = 0x03,
    CAN_PROTOCOL_STATUS_CAN_ERROR = 0x04
} can_protocol_status_e;

/**
 * @brief PID 参数编号。
 */
typedef enum
{
    CAN_PROTOCOL_PID_PARAM_P = 0x00,
    CAN_PROTOCOL_PID_PARAM_I = 0x01,
    CAN_PROTOCOL_PID_PARAM_D = 0x02,
    CAN_PROTOCOL_PID_PARAM_I_ACC_MAX = 0x03,
    CAN_PROTOCOL_PID_PARAM_OUT_MAX = 0x04
} can_protocol_pid_param_e;

/**
 * @brief FOC 配置参数编号。
 */
typedef enum
{
    CAN_PROTOCOL_FOC_CONFIG_POLE_PAIRS = 0x00,
    CAN_PROTOCOL_FOC_CONFIG_MASTER_VOLTAGE_MV = 0x01,
    CAN_PROTOCOL_FOC_CONFIG_CONTROL_HZ = 0x02,
    CAN_PROTOCOL_FOC_CONFIG_SENSOR_HZ = 0x03,
    CAN_PROTOCOL_FOC_CONFIG_PHASE_MAP = 0x04
} can_protocol_foc_config_param_e;

/**
 * @brief 初始化 CAN 协议层并配置过滤器。
 * @return void
 */
void can_protocol_init(void);

/**
 * @brief 轮询 CAN 协议层周期任务。
 * @return void
 */
void can_protocol_poll(void);

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
 * @brief 判断当前是否存在待处理的 CAN 协议数据。
 * @return int 存在待处理数据返回 1，否则返回 0。
 */
int can_protocol_has_pending(void);

/**
 * @brief 获取当前协议层使用的节点 ID。
 * @return uint16_t 当前节点 ID，单位：无。
 */
uint16_t can_protocol_get_node_id(void);

#ifdef __cplusplus
}
#endif

#endif
