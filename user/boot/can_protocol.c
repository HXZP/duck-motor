#include "boot/can_protocol.h"

#include "boot/boot_ota_def.h"
#include "boot/ota_process.h"
#include "common/user_info.h"

#include <string.h>

#define CAN_PROTOCOL_RX_QUEUE_LENGTH  (128u)
#define CAN_PROTOCOL_OTA_REQUEST_PREFIX_LEN  (3u)
#define CAN_PROTOCOL_OTA_REQUEST_LEN         (4u)
#define CAN_PROTOCOL_SEND_TIMEOUT_MS  (10u)

/**
 * @brief Boot CAN 接收帧缓存。
 */
typedef struct
{
    CAN_RxHeaderTypeDef header; /**< CAN 接收帧头。 */
    uint8_t data[8];            /**< CAN 接收载荷，单位：字节。 */
} can_protocol_rx_frame_t;

static const uint8_t s_can_protocol_ota_request_prefix[CAN_PROTOCOL_OTA_REQUEST_PREFIX_LEN] =
{
    0x7Fu,
    0x5Au,
    0xA5u,
};

static can_protocol_rx_frame_t can_protocol_rx_queue[CAN_PROTOCOL_RX_QUEUE_LENGTH];
static volatile uint8_t can_protocol_rx_write_index = 0u;
static volatile uint8_t can_protocol_rx_read_index = 0u;
static volatile uint8_t can_protocol_rx_overflow_flag = 0u;
static uint16_t can_protocol_node_id = CAN_PROTOCOL_DEFAULT_NODE_ID;

/**
 * @brief 规范化节点 ID。
 * @param node_id 待检查节点 ID，单位：无。
 * @return uint16_t 合法节点 ID，非法时返回默认值。
 */
static uint16_t can_protocol_normalize_node_id(uint32_t node_id)
{
    if ((node_id == 0u) || (node_id > 0x7Fu))
    {
        return CAN_PROTOCOL_DEFAULT_NODE_ID;
    }

    return (uint16_t)node_id;
}

/**
 * @brief 获取 OTA 控制和数据帧的标准帧 ID。
 * @return uint16_t 标准帧 ID，单位：无。
 */
static uint16_t can_protocol_get_ota_control_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_OTA_CONTROL_BASE_ID + can_protocol_node_id);
}

/**
 * @brief 获取 OTA 响应帧的标准帧 ID。
 * @return uint16_t 标准帧 ID，单位：无。
 */
static uint16_t can_protocol_get_ota_response_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_OTA_RESPONSE_BASE_ID + can_protocol_node_id);
}

/**
 * @brief 判断接收帧是否为 OTA 启动请求。
 * @param rxframe 接收帧头。
 * @param rx_data 接收数据缓冲区。
 * @return uint8_t 是 OTA 启动请求返回 1，否则返回 0。
 */
static uint8_t can_protocol_is_ota_request(CAN_RxHeaderTypeDef rxframe, const uint8_t *rx_data)
{
    if (rx_data == NULL)
    {
        return 0u;
    }

    if (rxframe.DLC < CAN_PROTOCOL_OTA_REQUEST_LEN)
    {
        return 0u;
    }

    if (memcmp(rx_data,
               s_can_protocol_ota_request_prefix,
               CAN_PROTOCOL_OTA_REQUEST_PREFIX_LEN) != 0)
    {
        return 0u;
    }

    if ((rx_data[3] == 0x0Au) ||
        (rx_data[3] == (uint8_t)can_protocol_node_id))
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief 根据当前节点 ID 更新 CAN 过滤器。
 * @return HAL_StatusTypeDef HAL 配置状态。
 */
static HAL_StatusTypeDef can_protocol_apply_filter(void)
{
    CAN_FilterTypeDef filter = {0};
    uint16_t ota_control_id;

    ota_control_id = can_protocol_get_ota_control_std_id();

    HAL_CAN_Stop(&hcan);

    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint16_t)(ota_control_id << 5);
    filter.FilterIdLow = 0x0000u;
    filter.FilterMaskIdHigh = 0xFFE0u;
    filter.FilterMaskIdLow = 0x0006u;
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
 * @brief 发送一帧标准 CAN 数据帧。
 * @param std_id 目标标准帧 ID，单位：无。
 * @param data 待发送数据缓冲区。
 * @param dlc 数据长度，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int can_protocol_send_frame(uint16_t std_id, const uint8_t *data, uint8_t dlc)
{
    CAN_TxHeaderTypeDef tx_header = {0};
    uint32_t tx_mailbox = 0u;
    uint32_t start_tick;
    uint8_t tx_data[8] = {0};

    if ((dlc > 8u) || ((data == NULL) && (dlc != 0u)))
    {
        return BOOT_ERR_PARAM;
    }

    if (dlc > 0u)
    {
        memcpy(tx_data, data, dlc);
    }

    tx_header.StdId = std_id;
    tx_header.ExtId = 0u;
    tx_header.IDE = CAN_ID_STD;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = dlc;
    tx_header.TransmitGlobalTime = DISABLE;

    start_tick = HAL_GetTick();
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0u)
    {
        if ((HAL_GetTick() - start_tick) >= CAN_PROTOCOL_SEND_TIMEOUT_MS)
        {
            return BOOT_ERR_TIMEOUT;
        }
    }

    if (HAL_CAN_AddTxMessage(&hcan, &tx_header, tx_data, &tx_mailbox) != HAL_OK)
    {
        return BOOT_ERR;
    }

    return BOOT_OK;
}

/**
 * @brief 将接收到的 CAN 帧压入协议接收队列。
 * @param header 接收帧头指针。
 * @param data 接收数据指针。
 * @return uint8_t 入队成功返回 1，失败返回 0。
 */
static uint8_t can_protocol_rx_queue_push(const CAN_RxHeaderTypeDef *header, const uint8_t *data)
{
    uint8_t next_write_index;

    if ((header == NULL) || (data == NULL))
    {
        return 0u;
    }

    next_write_index = (uint8_t)((can_protocol_rx_write_index + 1u) % CAN_PROTOCOL_RX_QUEUE_LENGTH);
    if (next_write_index == can_protocol_rx_read_index)
    {
        can_protocol_rx_overflow_flag = 1u;
        return 0u;
    }

    can_protocol_rx_queue[can_protocol_rx_write_index].header = *header;
    memcpy(can_protocol_rx_queue[can_protocol_rx_write_index].data, data, 8u);
    can_protocol_rx_write_index = next_write_index;

    return 1u;
}

/**
 * @brief 从协议接收队列弹出一帧 CAN 数据。
 * @param frame 输出帧指针。
 * @return uint8_t 出队成功返回 1，队列为空返回 0。
 */
static uint8_t can_protocol_rx_queue_pop(can_protocol_rx_frame_t *frame)
{
    if (frame == NULL)
    {
        return 0u;
    }

    if (can_protocol_rx_read_index == can_protocol_rx_write_index)
    {
        return 0u;
    }

    *frame = can_protocol_rx_queue[can_protocol_rx_read_index];
    can_protocol_rx_read_index = (uint8_t)((can_protocol_rx_read_index + 1u) % CAN_PROTOCOL_RX_QUEUE_LENGTH);

    return 1u;
}

/**
 * @brief 初始化 Boot CAN 协议层并配置 OTA 接收过滤器。
 * @return void
 */
void can_protocol_init(void)
{
    user_info_data_t info;

    if (UserInfo_Load(&info) == USER_INFO_OK)
    {
        can_protocol_node_id = can_protocol_normalize_node_id(info.can_id);
    }
    else
    {
        can_protocol_node_id = CAN_PROTOCOL_DEFAULT_NODE_ID;
    }

    can_protocol_apply_filter();
}

/**
 * @brief 获取当前 Boot CAN 节点 ID。
 * @return uint16_t 当前节点 ID，单位：无。
 */
uint16_t can_protocol_get_node_id(void)
{
    return can_protocol_node_id;
}

/**
 * @brief 发送 YMODEM 响应字节。
 * @param data 待发送数据缓冲区。
 * @param len 待发送长度，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int can_protocol_send_ota_response(const uint8_t *data, uint32_t len)
{
    if ((data == NULL) || (len == 0u) || (len > 8u))
    {
        return BOOT_ERR_PARAM;
    }

    return can_protocol_send_frame(can_protocol_get_ota_response_std_id(), data, (uint8_t)len);
}

/**
 * @brief 处理一帧接收到的 Boot CAN 协议数据。
 * @param rxframe 接收帧头。
 * @param rx_data 接收数据缓冲区。
 * @return void
 */
void CAN_protocol_analysis(CAN_RxHeaderTypeDef rxframe, uint8_t *rx_data)
{
    if (rx_data == NULL)
    {
        return;
    }

    if (rxframe.StdId != can_protocol_get_ota_control_std_id())
    {
        return;
    }

    if (OtaProcess_IsTransferEnabled() != 0u)
    {
        OtaProcess_PushRxBytes(rx_data, rxframe.DLC);
        return;
    }

    if (can_protocol_is_ota_request(rxframe, rx_data) != 0u)
    {
        OtaProcess_SetRequestPending();
    }
}

/**
 * @brief 在主循环上下文中处理待执行的 CAN 命令。
 * @return void
 */
void can_protocol_process(void)
{
    can_protocol_rx_frame_t frame = {0};

    while (can_protocol_rx_queue_pop(&frame) != 0u)
    {
        CAN_protocol_analysis(frame.header, frame.data);
    }

    if (can_protocol_rx_overflow_flag != 0u)
    {
        can_protocol_rx_overflow_flag = 0u;
    }
}

/**
 * @brief CAN FIFO0 接收中断回调函数。
 * @param hcan_handle CAN 句柄指针。
 * @return void
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_handle)
{
    CAN_RxHeaderTypeDef rx_header = {0};
    uint8_t rx_data[8] = {0};

    if (HAL_CAN_GetRxMessage(hcan_handle, CAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK)
    {
        can_protocol_rx_queue_push(&rx_header, rx_data);
    }
}
