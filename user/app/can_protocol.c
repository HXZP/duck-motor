#include "app/can_protocol.h"
#include "app/flash.h"
#include "app/foc_init.h"
#include <string.h>

static uint16_t can_protocol_node_id = CAN_PROTOCOL_DEFAULT_NODE_ID;

#define CAN_PROTOCOL_RX_QUEUE_LENGTH (8U)

typedef struct
{
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
} can_protocol_rx_frame_t;

static can_protocol_rx_frame_t can_protocol_rx_queue[CAN_PROTOCOL_RX_QUEUE_LENGTH];
static volatile uint8_t can_protocol_rx_write_index = 0U;
static volatile uint8_t can_protocol_rx_read_index = 0U;
static volatile uint8_t can_protocol_rx_overflow_flag = 0U;

/**
 * @brief 规范化节点 ID。
 * @param node_id 待检查的节点 ID。
 * @return uint16_t 合法节点 ID，非法时返回默认值。
 */
static uint16_t can_protocol_normalize_node_id(uint32_t node_id)
{
    if ((node_id == 0U) || (node_id > 0x7FU))
    {
        return CAN_PROTOCOL_DEFAULT_NODE_ID;
    }

    return (uint16_t)node_id;
}

/**
 * @brief 获取主机下发命令使用的标准帧 ID。
 * @return uint16_t 命令标准帧 ID。
 */
static uint16_t can_protocol_get_command_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_HOST_COMMAND_BASE_ID + can_protocol_node_id);
}

/**
 * @brief 获取电机应答使用的标准帧 ID。
 * @return uint16_t 应答标准帧 ID。
 */
static uint16_t can_protocol_get_ack_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_MOTOR_ACK_BASE_ID + can_protocol_node_id);
}

/**
 * @brief 获取电机主动上报使用的标准帧 ID。
 * @return uint16_t 上报标准帧 ID。
 */
static uint16_t can_protocol_get_report_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_MOTOR_REPORT_BASE_ID + can_protocol_node_id);
}

/**
 * @brief 将 32 位整数按小端格式写入缓冲区。
 * @param data 目标缓冲区指针。
 * @param value 待写入的 32 位整数。
 * @return void
 */
static void can_protocol_encode_int32(uint8_t *data, int32_t value)
{
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
    data[2] = (uint8_t)((value >> 16) & 0xFF);
    data[3] = (uint8_t)((value >> 24) & 0xFF);
}

/**
 * @brief 从小端缓冲区读取 32 位整数。
 * @param data 源缓冲区指针。
 * @return int32_t 解析后的 32 位整数。
 */
static int32_t can_protocol_decode_int32(const uint8_t *data)
{
    return (int32_t)(
        ((uint32_t)data[0])
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24)
    );
}

/**
 * @brief 发送一帧标准 CAN 数据帧。
 * @param std_id 目标标准帧 ID。
 * @param data 待发送数据指针。
 * @param dlc 数据长度。
 * @return HAL_StatusTypeDef HAL 发送状态。
 */
static HAL_StatusTypeDef can_protocol_send_frame(uint16_t std_id, const uint8_t *data, uint8_t dlc)
{
    CAN_TxHeaderTypeDef tx_header = {0};
    uint32_t tx_mailbox = 0;
    uint8_t tx_data[8] = {0};

    if (dlc > 8U)
    {
        return HAL_ERROR;
    }

    if ((data != NULL) && (dlc > 0U))
    {
        memcpy(tx_data, data, dlc);
    }

    tx_header.StdId = std_id;
    tx_header.ExtId = 0U;
    tx_header.IDE = CAN_ID_STD;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = dlc;

    return HAL_CAN_AddTxMessage(&hcan, &tx_header, tx_data, &tx_mailbox);
}

/**
 * @brief 发送协议应答帧。
 * @param cmd 回显命令码。
 * @param status 应答状态码。
 * @param payload 附加负载指针。
 * @param payload_len 附加负载长度。
 * @return void
 */
static void can_protocol_send_ack(uint8_t cmd, uint8_t status, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t ack_data[8] = {0};

    if (payload_len > 6U)
    {
        payload_len = 6U;
    }

    ack_data[0] = cmd;
    ack_data[1] = status;

    if ((payload != NULL) && (payload_len > 0U))
    {
        memcpy(&ack_data[2], payload, payload_len);
    }

    (void)can_protocol_send_frame(can_protocol_get_ack_std_id(), ack_data, 8U);
}

/**
 * @brief 将当前节点 ID 保存到 Flash。
 * @param node_id 待保存的节点 ID。
 * @return void
 */
static void can_protocol_store_node_id(uint16_t node_id)
{
    recoder_data data = {0};

    (void)Load_Recoder(&data);

    data.can_id = node_id;
    Save_Recoder(data);
}

/**
 * @brief 将当前零点角度保存到 Flash。
 * @param zero_angle 待保存的零点角度。
 * @return void
 */
static void can_protocol_store_zero_angle(int32_t zero_angle)
{
    recoder_data data = {0};

    (void)Load_Recoder(&data);

    data.calibration_angle = zero_angle;
    Save_Recoder(data);
}

/**
 * @brief 根据当前节点 ID 更新 CAN 过滤器。
 * @return HAL_StatusTypeDef HAL 配置状态。
 */
static HAL_StatusTypeDef can_protocol_apply_filter(void)
{
    CAN_FilterTypeDef filter = {0};
    uint16_t command_id = can_protocol_get_command_std_id();

    (void)HAL_CAN_Stop(&hcan);

    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint16_t)(command_id << 5);
    filter.FilterIdLow = 0x0000;
    filter.FilterMaskIdHigh = 0xFFE0;
    filter.FilterMaskIdLow = 0x0006;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_CAN_Start(&hcan);
}

/**
 * @brief 根据参数编号设置速度环参数。
 * @param param_id 参数编号。
 * @param raw_value 原始参数值。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_set_speed_pid_param(uint8_t param_id, int32_t raw_value)
{
    float p = 0.0f;
    float i = 0.0f;
    float d = 0.0f;
    float i_acc_max = 0.0f;
    float out_max = 0.0f;

    foc_speed_pid_get_full_param(&p, &i, &d, &i_acc_max, &out_max);

    switch (param_id)
    {
        case CAN_PROTOCOL_PID_PARAM_P:
        {
            p = (float)raw_value / 1000.0f;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I:
        {
            i = (float)raw_value / 1000.0f;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_D:
        {
            d = (float)raw_value / 1000.0f;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I_ACC_MAX:
        {
            i_acc_max = (float)raw_value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_OUT_MAX:
        {
            out_max = (float)raw_value;
            break;
        }

        default:
        {
            return CAN_PROTOCOL_STATUS_INVALID_PARAM;
        }
    }

    foc_speed_pid_set_full_param(p, i, d, i_acc_max, out_max);

    return CAN_PROTOCOL_STATUS_OK;
}

/**
 * @brief 根据参数编号读取速度环参数。
 * @param param_id 参数编号。
 * @param raw_value 参数值输出指针。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_get_speed_pid_param(uint8_t param_id, int32_t *raw_value)
{
    float p = 0.0f;
    float i = 0.0f;
    float d = 0.0f;
    float i_acc_max = 0.0f;
    float out_max = 0.0f;

    foc_speed_pid_get_full_param(&p, &i, &d, &i_acc_max, &out_max);

    switch (param_id)
    {
        case CAN_PROTOCOL_PID_PARAM_P:
        {
            *raw_value = (int32_t)(p * 1000.0f);
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I:
        {
            *raw_value = (int32_t)(i * 1000.0f);
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_D:
        {
            *raw_value = (int32_t)(d * 1000.0f);
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I_ACC_MAX:
        {
            *raw_value = (int32_t)i_acc_max;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_OUT_MAX:
        {
            *raw_value = (int32_t)out_max;
            break;
        }

        default:
        {
            return CAN_PROTOCOL_STATUS_INVALID_PARAM;
        }
    }

    return CAN_PROTOCOL_STATUS_OK;
}

/**
 * @brief 根据参数编号设置位置环参数。
 * @param param_id 参数编号。
 * @param raw_value 原始参数值。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_set_position_pid_param(uint8_t param_id, int32_t raw_value)
{
    float p = 0.0f;
    float i = 0.0f;
    float d = 0.0f;
    float i_acc_max = 0.0f;
    float out_max = 0.0f;

    foc_position_pid_get_param(&p, &i, &d, &i_acc_max, &out_max);

    switch (param_id)
    {
        case CAN_PROTOCOL_PID_PARAM_P:
        {
            p = (float)raw_value / 1000.0f;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I:
        {
            i = (float)raw_value / 1000.0f;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_D:
        {
            d = (float)raw_value / 1000.0f;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I_ACC_MAX:
        {
            i_acc_max = (float)raw_value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_OUT_MAX:
        {
            out_max = (float)raw_value;
            break;
        }

        default:
        {
            return CAN_PROTOCOL_STATUS_INVALID_PARAM;
        }
    }

    foc_position_pid_set_param(p, i, d, i_acc_max, out_max);

    return CAN_PROTOCOL_STATUS_OK;
}

/**
 * @brief 根据参数编号读取位置环参数。
 * @param param_id 参数编号。
 * @param raw_value 参数值输出指针。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_get_position_pid_param(uint8_t param_id, int32_t *raw_value)
{
    float p = 0.0f;
    float i = 0.0f;
    float d = 0.0f;
    float i_acc_max = 0.0f;
    float out_max = 0.0f;

    foc_position_pid_get_param(&p, &i, &d, &i_acc_max, &out_max);

    switch (param_id)
    {
        case CAN_PROTOCOL_PID_PARAM_P:
        {
            *raw_value = (int32_t)(p * 1000.0f);
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I:
        {
            *raw_value = (int32_t)(i * 1000.0f);
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_D:
        {
            *raw_value = (int32_t)(d * 1000.0f);
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I_ACC_MAX:
        {
            *raw_value = (int32_t)i_acc_max;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_OUT_MAX:
        {
            *raw_value = (int32_t)out_max;
            break;
        }

        default:
        {
            return CAN_PROTOCOL_STATUS_INVALID_PARAM;
        }
    }

    return CAN_PROTOCOL_STATUS_OK;
}

/**
 * @brief 将接收到的 CAN 帧压入协议接收队列。
 * @param header 接收帧头指针。
 * @param data 接收数据指针。
 * @return uint8_t 入队成功返回 1，失败返回 0。
 */
static uint8_t can_protocol_rx_queue_push(const CAN_RxHeaderTypeDef *header, const uint8_t *data)
{
    uint8_t next_write_index = 0U;

    next_write_index = (uint8_t)((can_protocol_rx_write_index + 1U) % CAN_PROTOCOL_RX_QUEUE_LENGTH);

    if (next_write_index == can_protocol_rx_read_index)
    {
        can_protocol_rx_overflow_flag = 1U;
        return 0U;
    }

    can_protocol_rx_queue[can_protocol_rx_write_index].header = *header;
    memcpy(can_protocol_rx_queue[can_protocol_rx_write_index].data, data, 8U);
    can_protocol_rx_write_index = next_write_index;

    return 1U;
}

/**
 * @brief 从协议接收队列弹出一帧 CAN 数据。
 * @param frame 输出帧指针。
 * @return uint8_t 出队成功返回 1，队列为空返回 0。
 */
static uint8_t can_protocol_rx_queue_pop(can_protocol_rx_frame_t *frame)
{
    if (can_protocol_rx_read_index == can_protocol_rx_write_index)
    {
        return 0U;
    }

    *frame = can_protocol_rx_queue[can_protocol_rx_read_index];
    can_protocol_rx_read_index = (uint8_t)((can_protocol_rx_read_index + 1U) % CAN_PROTOCOL_RX_QUEUE_LENGTH);

    return 1U;
}

#if 0
static void can_protocol_legacy_analysis(CAN_RxHeaderTypeDef rxframe, uint8_t *RxData)
{
    switch(rxframe.StdId)
    {
        case 0x100:
        {
            if(RxData[0])
            {
                foc_set_state(&foc, Foc_Shutdown);
            }
            else
            {
                foc_set_state(&foc, Foc_Working);
            }
            break;        
        }
        
        case 0x105:
        {
            foc_speed_pid_set_param(
            ((float)((RxData[1]<<8)|RxData[0]))/1000, 
            ((float)((RxData[3]<<8)|RxData[2]))/1000,  
            ((RxData[5]<<8)|RxData[4]), 
            ((RxData[7]<<8)|RxData[6])
            );
            break;        
        }
        
        case 0x106:
        {
            foc_speed_pid_set_target((int32_t)RxData[0]);
            break;        
        }
    }
}

/* 重写 HAL_CAN_RxFifo0MsgPendingCallback，处理 FIFO0 的数据 */
static void can_protocol_legacy_callback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef RxHeader;
    uint8_t RxData[8];
    
    // 注意：回调函数内仍需调用 GetRxMessage 来读取并释放FIFO
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK)
    {
        printf("CAN ID: 0x%08lX, DLC: %d, Data: ", RxHeader.StdId, RxHeader.DLC);
        for (int i = 0; i < RxHeader.DLC; i++)
        {
            printf("%02X ", RxData[i]);
        }
        printf("\r\n");
        // 在这里处理接收到的数据 RxData
        // 可以通过 RxHeader.StdId 或 RxHeader.ExtId 来区分不同报文
        // 这里就是你的业务逻辑处理位置，与之前写在ISR里一样
    }
}
#endif

/**
 * @brief 初始化 CAN 协议层并配置过滤器。
 * @return void
 */
void can_protocol_init(void)
{
    recoder_data data = {0};

    (void)Load_Recoder(&data);
    can_protocol_node_id = can_protocol_normalize_node_id(data.can_id);

    (void)can_protocol_apply_filter();
}

/**
 * @brief 获取当前协议层使用的节点 ID。
 * @return uint16_t 当前节点 ID。
 */
uint16_t can_protocol_get_node_id(void)
{
    return can_protocol_node_id;
}

/**
 * @brief 主动上报当前电机位置和速度。
 * @return void
 */
void can_protocol_report_motor_state(void)
{
    uint8_t report_data[8] = {0};

    report_data[0] = CAN_PROTOCOL_CMD_REPORT_POSITION;
    can_protocol_encode_int32(&report_data[1], foc_get_angle(&foc));
    (void)can_protocol_send_frame(can_protocol_get_report_std_id(), report_data, 8U);

    memset(report_data, 0, sizeof(report_data));
    report_data[0] = CAN_PROTOCOL_CMD_REPORT_SPEED;
    can_protocol_encode_int32(&report_data[1], foc_get_speed_estimate());
    (void)can_protocol_send_frame(can_protocol_get_report_std_id(), report_data, 8U);
}

/**
 * @brief 在主循环上下文中处理待执行的 CAN 命令。
 * @return void
 */
void can_protocol_process(void)
{
    can_protocol_rx_frame_t frame = {0};

    while (can_protocol_rx_queue_pop(&frame) != 0U)
    {
        CAN_protocol_analysis(frame.header, frame.data);
    }

    if (can_protocol_rx_overflow_flag != 0U)
    {
        can_protocol_rx_overflow_flag = 0U;
    }
}

/**
 * @brief 处理一帧接收到的 CAN 协议数据。
 * @param rxframe 接收帧头。
 * @param rx_data 接收到的数据区指针。
 * @return void
 */
void CAN_protocol_analysis(CAN_RxHeaderTypeDef rxframe, uint8_t *rx_data)
{
    uint8_t ack_payload[6] = {0};
    int32_t value = 0;
    uint8_t status = CAN_PROTOCOL_STATUS_OK;

    if (rxframe.StdId != can_protocol_get_command_std_id())
    {
        return;
    }

    if (rxframe.DLC == 0U)
    {
        can_protocol_send_ack(0U, CAN_PROTOCOL_STATUS_INVALID_PARAM, NULL, 0U);
        return;
    }

    switch (rx_data[0])
    {
        case CAN_PROTOCOL_CMD_SET_MODE:
        {
            if (rxframe.DLC < 3U)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            if (rx_data[2] > (uint8_t)FOC_CTRL_MODE_POSITION)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            foc_control_mode_set((foc_ctrl_mode_t)rx_data[2]);

            if (rx_data[1] == 0U)
            {
                foc_set_state(&foc, Foc_Shutdown);
            }
            else
            {
                foc_set_state(&foc, Foc_Working);
            }

            ack_payload[0] = rx_data[1];
            ack_payload[1] = rx_data[2];
            can_protocol_send_ack(rx_data[0], status, ack_payload, 2U);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_SPEED_TARGET:
        {
            if (rxframe.DLC < 5U)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            if (foc_control_mode_get() != FOC_CTRL_MODE_SPEED)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_MODE;
                break;
            }

            value = can_protocol_decode_int32(&rx_data[1]);
            foc_speed_pid_set_target((float)value);
            can_protocol_encode_int32(ack_payload, value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4U);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_POSITION_TARGET:
        {
            if (rxframe.DLC < 5U)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            if (foc_control_mode_get() != FOC_CTRL_MODE_POSITION)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_MODE;
                break;
            }

            value = can_protocol_decode_int32(&rx_data[1]);
            foc_position_pid_set_target((float)value);
            can_protocol_encode_int32(ack_payload, value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4U);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_CURRENT_TARGET:
        {
            if (rxframe.DLC < 5U)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            if (foc_control_mode_get() != FOC_CTRL_MODE_CURRENT)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_MODE;
                break;
            }

            value = can_protocol_decode_int32(&rx_data[1]);
            value = foc_current_set_target(value);
            can_protocol_encode_int32(ack_payload, value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4U);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_SPEED_PID_PARAM:
        {
            if (rxframe.DLC < 6U)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            value = can_protocol_decode_int32(&rx_data[2]);
            status = can_protocol_set_speed_pid_param(rx_data[1], value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_int32(&ack_payload[1], value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5U);
            return;
        }

        case CAN_PROTOCOL_CMD_GET_SPEED_PID_PARAM:
        {
            if (rxframe.DLC < 2U)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            status = can_protocol_get_speed_pid_param(rx_data[1], &value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_int32(&ack_payload[1], value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5U);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_POSITION_PID_PARAM:
        {
            if (rxframe.DLC < 6U)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            value = can_protocol_decode_int32(&rx_data[2]);
            status = can_protocol_set_position_pid_param(rx_data[1], value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_int32(&ack_payload[1], value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5U);
            return;
        }

        case CAN_PROTOCOL_CMD_GET_POSITION_PID_PARAM:
        {
            if (rxframe.DLC < 2U)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            status = can_protocol_get_position_pid_param(rx_data[1], &value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_int32(&ack_payload[1], value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5U);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_CAN_ID:
        {
            uint16_t new_node_id = 0;

            if (rxframe.DLC < 2U)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            new_node_id = can_protocol_normalize_node_id(rx_data[1]);

            if (new_node_id != rx_data[1])
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            ack_payload[0] = (uint8_t)new_node_id;
            can_protocol_send_ack(rx_data[0], status, ack_payload, 1U);

            can_protocol_store_node_id(new_node_id);
            can_protocol_node_id = new_node_id;

            if (can_protocol_apply_filter() != HAL_OK)
            {
                can_protocol_send_ack(rx_data[0], CAN_PROTOCOL_STATUS_CAN_ERROR, NULL, 0U);
            }

            return;
        }

        case CAN_PROTOCOL_CMD_GET_CAN_ID:
        {
            ack_payload[0] = (uint8_t)can_protocol_node_id;
            can_protocol_send_ack(rx_data[0], status, ack_payload, 1U);
            return;
        }

        case CAN_PROTOCOL_CMD_ZERO_CALIBRATE:
        {
            value = foc_zero_angle_reset();
            can_protocol_store_zero_angle(value);
            can_protocol_encode_int32(ack_payload, value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4U);
            return;
        }

        case CAN_PROTOCOL_CMD_GET_ZERO:
        {
            value = foc_get_zero_angle();
            can_protocol_encode_int32(ack_payload, value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4U);
            return;
        }

        default:
        {
            status = CAN_PROTOCOL_STATUS_INVALID_CMD;
            break;
        }
    }

    can_protocol_send_ack(rx_data[0], status, NULL, 0U);
}

/**
 * @brief 接收 FIFO0 消息到达回调。
 * @param hcan_handle CAN 句柄指针。
 * @return void
 */
/**
 * @brief CAN FIFO0 接收中断回调函数。
 * @param hcan CAN 句柄指针。
 * @return void
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx_header = {0};
    uint8_t rx_data[8] = {0};

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK)
    {
        (void)can_protocol_rx_queue_push(&rx_header, rx_data);
    }
}
