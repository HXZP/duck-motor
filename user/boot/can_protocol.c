#include "boot/can_protocol.h"
#include <string.h>

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
 * @brief 根据当前节点 ID 更新 CAN 过滤器。
 * @return HAL_StatusTypeDef HAL 配置状态。
 */
static HAL_StatusTypeDef can_protocol_apply_filter(void)
{
    CAN_FilterTypeDef filter = {0};

    (void)HAL_CAN_Stop(&hcan);

    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint16_t)(0 << 5);
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

/**
 * @brief 初始化 CAN 协议层并配置过滤器。
 * @return void
 */
void can_protocol_init(void)
{
    (void)can_protocol_apply_filter();
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
//        CAN_protocol_analysis(frame.header, frame.data);
    }

    if (can_protocol_rx_overflow_flag != 0U)
    {
        can_protocol_rx_overflow_flag = 0U;
    }
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
