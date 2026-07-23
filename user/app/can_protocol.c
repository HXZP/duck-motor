#include "app/can_protocol.h"

#include "app/app_error.h"
#include "app/app_light.h"
#include "app/app_version.h"
#include "app/as5600.h"
#include "app/flash.h"
#include "app/foc_app.h"
#include "app/foc_config.h"
#include "app/log.h"
#include "common/chip_uid.h"

#include <math.h>
#include <string.h>

#define CAN_PROTOCOL_RX_QUEUE_LENGTH                 8u
#define CAN_PROTOCOL_FULL_DLC                        8u
#define CAN_PROTOCOL_MANAGE_REPORT_PERIOD_MS         500u
#define CAN_PROTOCOL_MANAGE_IDENTIFY_DEFAULT_S       5u
#define CAN_PROTOCOL_MANAGE_IDENTIFY_MAX_S           60u
#define CAN_PROTOCOL_OTA_START_DLC                   4u
#define CAN_PROTOCOL_REPORT_CONFIG_DLC               4u
#define CAN_PROTOCOL_RESET_CAN_CONFIG_DLC            2u
#define CAN_PROTOCOL_RESET_CAN_CONFIG_CONFIRM_CODE   0xA5u
#define CAN_PROTOCOL_RESET_DELAY_MS                  50u
#define CAN_PROTOCOL_UNCONFIGURED_LOG_PERIOD_MS      1000u
#define CAN_PROTOCOL_ERROR_REPORT_PERIOD_MS          1000u
#define CAN_PROTOCOL_PID_DEBUG_FRAME_PERIOD_MS       3u
#define CAN_PROTOCOL_PID_DEBUG_FRAME_COUNT           4u
#define CAN_PROTOCOL_FOC_TRACE_ACTION_START           0u
#define CAN_PROTOCOL_FOC_TRACE_ACTION_STOP            1u
#define CAN_PROTOCOL_FOC_TRACE_ACTION_UPLOAD          2u
#define CAN_PROTOCOL_FOC_TRACE_PART_COUNT             3u
#define CAN_PROTOCOL_FOC_TRACE_PART_DATA_LENGTH       6u

/**
 * @brief CAN 接收帧缓存。
 */
typedef struct
{
    CAN_RxHeaderTypeDef header; /**< CAN 接收帧头。 */
    uint8_t data[8];            /**< CAN 接收载荷，单位：字节。 */
} can_protocol_rx_frame_t;

static uint16_t can_protocol_node_id = CAN_PROTOCOL_DEFAULT_NODE_ID;
static uint8_t can_protocol_is_configured = USER_INFO_CAN_CONFIGURED_NO;
static uint32_t can_protocol_short_uid = 0u;
static uint32_t can_protocol_next_manage_report_tick_ms = 0u;
static uint8_t can_protocol_report_enabled = USER_INFO_DEFAULT_REPORT_ENABLE;
static uint32_t can_protocol_report_period_ms = USER_INFO_DEFAULT_REPORT_PERIOD_MS;
static uint32_t can_protocol_next_motor_report_tick_ms = 0u;
static uint32_t can_protocol_next_pid_debug_report_tick_ms = 0u;
static uint8_t can_protocol_pid_debug_frame_index = 0u;
static foc_speed_pid_runtime_t can_protocol_pid_debug_runtime;
static uint8_t can_protocol_foc_trace_upload_active = 0u;
static uint16_t can_protocol_foc_trace_upload_index = 0u;
static uint8_t can_protocol_foc_trace_upload_part = 0u;
static uint32_t can_protocol_next_unconfigured_log_tick_ms = 0u;
static uint32_t can_protocol_next_error_report_tick_ms = 0u;
static uint8_t can_protocol_foc_available = 0u;
static uint32_t can_protocol_tx_ok_count = 0u;
static uint32_t can_protocol_tx_fail_count = 0u;
static uint32_t can_protocol_rx_count = 0u;
static uint32_t can_protocol_manage_report_count = 0u;
static uint32_t can_protocol_last_tx_error = 0u;

static can_protocol_rx_frame_t can_protocol_rx_queue[CAN_PROTOCOL_RX_QUEUE_LENGTH];
static volatile uint8_t can_protocol_rx_write_index = 0u;
static volatile uint8_t can_protocol_rx_read_index = 0u;
static volatile uint8_t can_protocol_rx_overflow_flag = 0u;

static void can_protocol_report_speed_pid_debug(void);
static void can_protocol_upload_foc_trace(void);

/**
 * @brief 判断节点 ID 是否允许作为业务节点 ID。
 * @param node_id 待检查节点 ID，单位：无。
 * @return uint8_t 合法返回 1，否则返回 0。
 */
static uint8_t can_protocol_is_valid_configured_node_id(uint32_t node_id)
{
    if ((node_id < USER_INFO_CAN_ID_MIN) || (node_id > USER_INFO_CAN_ID_MAX))
    {
        return 0u;
    }

    if (node_id == CAN_PROTOCOL_MANAGE_NODE_ID)
    {
        return 0u;
    }

    return 1u;
}

/**
 * @brief 获取主机下发命令使用的标准帧 ID。
 * @return uint16_t 命令标准帧 ID，单位：无。
 */
static uint16_t can_protocol_get_command_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_HOST_COMMAND_BASE_ID + can_protocol_node_id);
}

/**
 * @brief 获取 OTA 控制帧使用的标准帧 ID。
 * @return uint16_t OTA 控制标准帧 ID，单位：无。
 */
static uint16_t can_protocol_get_ota_control_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_OTA_CONTROL_BASE_ID + can_protocol_node_id);
}

/**
 * @brief 获取电机应答使用的标准帧 ID。
 * @return uint16_t 应答标准帧 ID，单位：无。
 */
static uint16_t can_protocol_get_ack_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_MOTOR_ACK_BASE_ID + can_protocol_node_id);
}

/**
 * @brief 获取电机主动上报使用的标准帧 ID。
 * @return uint16_t 上报标准帧 ID，单位：无。
 */
static uint16_t can_protocol_get_report_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_MOTOR_REPORT_BASE_ID + can_protocol_node_id);
}

/**
 * @brief 获取管理命令标准帧 ID。
 * @return uint16_t 管理命令标准帧 ID，单位：无。
 */
static uint16_t can_protocol_get_manage_command_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_HOST_COMMAND_BASE_ID + CAN_PROTOCOL_MANAGE_NODE_ID);
}

/**
 * @brief 获取管理应答标准帧 ID。
 * @return uint16_t 管理应答标准帧 ID，单位：无。
 */
static uint16_t can_protocol_get_manage_ack_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_MOTOR_ACK_BASE_ID + CAN_PROTOCOL_MANAGE_NODE_ID);
}

/**
 * @brief 获取管理上报标准帧 ID。
 * @return uint16_t 管理上报标准帧 ID，单位：无。
 */
static uint16_t can_protocol_get_manage_report_std_id(void)
{
    return (uint16_t)(CAN_PROTOCOL_MOTOR_REPORT_BASE_ID + CAN_PROTOCOL_MANAGE_NODE_ID);
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
 * @brief 将 32 位无符号整数按小端格式写入缓冲区。
 * @param data 目标缓冲区指针。
 * @param value 待写入的 32 位无符号整数。
 * @return void
 */
static void can_protocol_encode_uint32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
    data[2] = (uint8_t)((value >> 16) & 0xFFu);
    data[3] = (uint8_t)((value >> 24) & 0xFFu);
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
 * @brief 从小端缓冲区读取 32 位无符号整数。
 * @param data 源缓冲区指针。
 * @return uint32_t 解析后的 32 位无符号整数。
 */
static uint32_t can_protocol_decode_uint32(const uint8_t *data)
{
    return (uint32_t)(
        ((uint32_t)data[0])
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24)
    );
}

/**
 * @brief 将 16 位无符号整数按小端格式写入缓冲区。
 * @param data 目标缓冲区指针。
 * @param value 待写入的 16 位无符号整数。
 * @return void
 */
static void can_protocol_encode_uint16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

/**
 * @brief 从小端缓冲区读取 16 位无符号整数。
 * @param data 源缓冲区指针。
 * @return uint16_t 解析后的 16 位无符号整数。
 */
static uint16_t can_protocol_decode_uint16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0]) | ((uint16_t)data[1] << 8));
}

/**
 * @brief 发送一帧标准 CAN 数据帧。
 * @param std_id 目标标准帧 ID，单位：无。
 * @param data 待发送数据指针。
 * @param dlc 数据长度，单位：字节。
 * @return HAL_StatusTypeDef HAL 发送状态。
 */
static HAL_StatusTypeDef can_protocol_send_frame(uint16_t std_id, const uint8_t *data, uint8_t dlc)
{
    CAN_TxHeaderTypeDef tx_header = {0};
    uint32_t tx_mailbox = 0u;
    uint8_t tx_data[8] = {0};
    HAL_StatusTypeDef status;

    if (dlc > CAN_PROTOCOL_FULL_DLC)
    {
        return HAL_ERROR;
    }

    if ((data != NULL) && (dlc > 0u))
    {
        memcpy(tx_data, data, dlc);
    }

    tx_header.StdId = std_id;
    tx_header.ExtId = 0u;
    tx_header.IDE = CAN_ID_STD;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = dlc;

    status = HAL_CAN_AddTxMessage(&hcan, &tx_header, tx_data, &tx_mailbox);
    if (status == HAL_OK)
    {
        can_protocol_tx_ok_count++;
    }
    else
    {
        can_protocol_tx_fail_count++;
        can_protocol_last_tx_error = HAL_CAN_GetError(&hcan);
        printf("CAN tx fail: id=0x%03X status=%d err=0x%08lX state=%lu\r\n",
               std_id,
               (int)status,
               (unsigned long)can_protocol_last_tx_error,
               (unsigned long)HAL_CAN_GetState(&hcan));
    }

    return status;
}

/**
 * @brief 以错误位图形式发送当前错误和本次上电锁存错误。
 * @return void
 */
static void can_protocol_send_error_report(void)
{
    uint8_t report_data[8] = {0};
    uint32_t latched_bits;

    latched_bits = AppError_GetLatched();
    if (latched_bits == APP_ERROR_NONE)
    {
        return;
    }

    can_protocol_encode_uint32(&report_data[0], AppError_GetActive());
    can_protocol_encode_uint32(&report_data[4], latched_bits);
    can_protocol_send_frame((uint16_t)(CAN_PROTOCOL_ERROR_REPORT_BASE_ID + can_protocol_node_id),
                            report_data,
                            CAN_PROTOCOL_FULL_DLC);
}

/**
 * @brief 发送指定 ID 的协议应答帧。
 * @param std_id 应答标准帧 ID，单位：无。
 * @param cmd 回显命令码。
 * @param status 应答状态码。
 * @param payload 附加负载指针。
 * @param payload_len 附加负载长度，单位：字节。
 * @return void
 */
static void can_protocol_send_ack_to(uint16_t std_id,
                                     uint8_t cmd,
                                     uint8_t status,
                                     const uint8_t *payload,
                                     uint8_t payload_len)
{
    uint8_t ack_data[8] = {0};

    if (payload_len > 6u)
    {
        payload_len = 6u;
    }

    ack_data[0] = cmd;
    ack_data[1] = status;

    if ((payload != NULL) && (payload_len > 0u))
    {
        memcpy(&ack_data[2], payload, payload_len);
    }

    can_protocol_send_frame(std_id, ack_data, CAN_PROTOCOL_FULL_DLC);
}

/**
 * @brief 发送当前业务节点应答帧。
 * @param cmd 回显命令码。
 * @param status 应答状态码。
 * @param payload 附加负载指针。
 * @param payload_len 附加负载长度，单位：字节。
 * @return void
 */
static void can_protocol_send_ack(uint8_t cmd, uint8_t status, const uint8_t *payload, uint8_t payload_len)
{
    can_protocol_send_ack_to(can_protocol_get_ack_std_id(), cmd, status, payload, payload_len);
}

/**
 * @brief 发送管理应答帧。
 * @param cmd 回显命令码。
 * @param status 应答状态码。
 * @param payload 附加负载指针。
 * @param payload_len 附加负载长度，单位：字节。
 * @return void
 */
static void can_protocol_send_manage_ack(uint8_t cmd, uint8_t status, const uint8_t *payload, uint8_t payload_len)
{
    can_protocol_send_ack_to(can_protocol_get_manage_ack_std_id(), cmd, status, payload, payload_len);
}

/**
 * @brief 判断当前帧是否为 OTA 启动请求。
 * @param rxframe 接收帧头。
 * @param rx_data 接收数据缓冲区。
 * @return uint8_t 是 OTA 启动请求返回 1，否则返回 0。
 */
static uint8_t can_protocol_is_ota_start_request(CAN_RxHeaderTypeDef rxframe, const uint8_t *rx_data)
{
    if (rx_data == NULL)
    {
        return 0u;
    }

    if (rxframe.DLC < CAN_PROTOCOL_OTA_START_DLC)
    {
        return 0u;
    }

    if ((rx_data[0] != 0x7Fu) ||
        (rx_data[1] != 0x5Au) ||
        (rx_data[2] != 0xA5u))
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
 * @brief 切换到 Boot OTA 模式。
 * @param cmd 当前命令码。
 * @return void
 */
static void can_protocol_enter_boot_ota(uint8_t cmd)
{
    Save_OtaFlag(USER_INFO_OTA_FLAG_BOOT);
    can_protocol_send_ack(cmd, CAN_PROTOCOL_STATUS_OK, NULL, 0u);
    HAL_Delay(10u);
    NVIC_SystemReset();
}

/**
 * @brief 将当前节点 ID 保存到 Flash。
 * @param node_id 待保存的节点 ID，单位：无。
 * @return void
 */
static void can_protocol_store_node_id(uint16_t node_id)
{
    recoder_data data = {0};

    if (Load_Recoder(&data) == 0u)
    {
        data.calibration_angle = foc_get_zero_angle();
        data.ota = USER_INFO_OTA_FLAG_APP;
        data.report_enabled = can_protocol_report_enabled;
        data.report_period_ms = can_protocol_report_period_ms;
    }

    data.can_id = node_id;
    data.can_configured = USER_INFO_CAN_CONFIGURED_YES;
    Save_Recoder(data);
}

/**
 * @brief 将当前零点角度保存到 Flash。
 * @param zero_angle 待保存的零点角度，单位：内部角度计数。
 * @return void
 */
static void can_protocol_store_zero_angle(int32_t zero_angle)
{
    recoder_data data = {0};

    if (Load_Recoder(&data) == 0u)
    {
        data.can_id = can_protocol_node_id;
        data.can_configured = can_protocol_is_configured;
        data.ota = USER_INFO_OTA_FLAG_APP;
    }

    data.calibration_angle = zero_angle;
    Save_Recoder(data);
}

/**
 * @brief 复位 CAN 配置信息。
 * @return uint8_t 成功返回 CAN_PROTOCOL_STATUS_OK，失败返回 CAN_PROTOCOL_STATUS_xxx。
 */
static uint8_t can_protocol_reset_can_config(void)
{
    user_info_data_t info;
    int ret;

    foc_current_set_target(0);
    foc_set_target(0, 0, 0);
    foc_set_state(&foc, Foc_Shutdown);
    foc_output_enable(0u);

    ret = UserInfo_Load(&info);
    if (ret != USER_INFO_OK)
    {
        ret = UserInfo_GetDefault(&info);
        if (ret != USER_INFO_OK)
        {
            return CAN_PROTOCOL_STATUS_CAN_ERROR;
        }
    }

    info.can_id = USER_INFO_MANAGE_CAN_ID;
    info.can_configured = USER_INFO_CAN_CONFIGURED_NO;
    if (UserInfo_Save(&info) != USER_INFO_OK)
    {
        return CAN_PROTOCOL_STATUS_CAN_ERROR;
    }

    return CAN_PROTOCOL_STATUS_OK;
}

/**
 * @brief 归一化主动上报配置。
 * @param enabled 上报使能状态指针。
 * @param period_ms 上报周期指针，单位：毫秒。
 * @return void
 */
static void can_protocol_normalize_report_config(uint32_t *enabled, uint32_t *period_ms)
{
    if ((enabled == NULL) || (period_ms == NULL))
    {
        return;
    }

    if (*enabled != USER_INFO_REPORT_ENABLED)
    {
        *enabled = USER_INFO_REPORT_DISABLED;
    }

    if ((*period_ms < USER_INFO_MIN_REPORT_PERIOD_MS) ||
        (*period_ms > USER_INFO_MAX_REPORT_PERIOD_MS))
    {
        *period_ms = USER_INFO_DEFAULT_REPORT_PERIOD_MS;
    }
}

/**
 * @brief 加载主动上报配置到运行态。
 * @param data App 参数记录。
 * @return void
 */
static void can_protocol_load_report_config(const recoder_data *data)
{
    uint32_t enabled;
    uint32_t period_ms;

    if (data == NULL)
    {
        enabled = USER_INFO_DEFAULT_REPORT_ENABLE;
        period_ms = USER_INFO_DEFAULT_REPORT_PERIOD_MS;
    }
    else
    {
        enabled = data->report_enabled;
        period_ms = data->report_period_ms;
    }

    can_protocol_normalize_report_config(&enabled, &period_ms);
    can_protocol_report_enabled = (uint8_t)enabled;
    can_protocol_report_period_ms = period_ms;
    can_protocol_next_motor_report_tick_ms = HAL_GetTick() + can_protocol_report_period_ms;
}

/**
 * @brief 保存主动上报配置。
 * @param enabled 上报使能状态，单位：无。
 * @param period_ms 上报周期，单位：毫秒。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_store_report_config(uint32_t enabled, uint32_t period_ms)
{
    recoder_data data = {0};

    can_protocol_normalize_report_config(&enabled, &period_ms);

    if (Load_Recoder(&data) == 0u)
    {
        data.calibration_angle = foc_get_zero_angle();
        data.can_id = can_protocol_node_id;
        data.can_configured = can_protocol_is_configured;
        data.ota = USER_INFO_OTA_FLAG_APP;
    }

    data.report_enabled = enabled;
    data.report_period_ms = period_ms;
    Save_Recoder(data);

    can_protocol_report_enabled = (uint8_t)enabled;
    can_protocol_report_period_ms = period_ms;
    can_protocol_next_motor_report_tick_ms = HAL_GetTick() + can_protocol_report_period_ms;

    return CAN_PROTOCOL_STATUS_OK;
}

/**
 * @brief 设置 FOC 基础配置参数。
 * @param param_id 参数编号。
 * @param raw_value 参数原始值，单位由参数编号决定。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_set_foc_config_param(uint8_t param_id, uint32_t raw_value)
{
    user_info_foc_config_t config;

    if (foc_config_get(&config) != USER_INFO_OK)
    {
        return CAN_PROTOCOL_STATUS_CAN_ERROR;
    }

    switch (param_id)
    {
        case CAN_PROTOCOL_FOC_CONFIG_POLE_PAIRS:
        {
            config.pole_pairs = raw_value;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_MASTER_VOLTAGE_MV:
        {
            config.master_voltage_mv = raw_value;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_CONTROL_HZ:
        {
            config.control_hz = raw_value;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_SENSOR_HZ:
        {
            config.sensor_hz = raw_value;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_PHASE_MAP:
        {
            config.phase_map = raw_value;
            break;
        }

        default:
        {
            return CAN_PROTOCOL_STATUS_INVALID_PARAM;
        }
    }

    if (UserInfo_ValidateFocConfig(&config) != USER_INFO_OK)
    {
        return CAN_PROTOCOL_STATUS_INVALID_PARAM;
    }

    if (Flash_SaveFocConfig(&config) != USER_INFO_OK)
    {
        return CAN_PROTOCOL_STATUS_CAN_ERROR;
    }

    if (foc_config_set(&config) != USER_INFO_OK)
    {
        return CAN_PROTOCOL_STATUS_CAN_ERROR;
    }

    return CAN_PROTOCOL_STATUS_OK;
}

/**
 * @brief 读取 FOC 基础配置参数。
 * @param param_id 参数编号。
 * @param raw_value 参数原始值输出缓冲区，单位由参数编号决定。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_get_foc_config_param(uint8_t param_id, uint32_t *raw_value)
{
    as5600_diagnostic_t as5600_diagnostic;
    user_info_foc_config_t config;

    if (raw_value == NULL)
    {
        return CAN_PROTOCOL_STATUS_INVALID_PARAM;
    }

    if (foc_config_get(&config) != USER_INFO_OK)
    {
        return CAN_PROTOCOL_STATUS_CAN_ERROR;
    }

    as5600GetDiagnostic(&as5600_diagnostic);

    switch (param_id)
    {
        case CAN_PROTOCOL_FOC_CONFIG_POLE_PAIRS:
        {
            *raw_value = config.pole_pairs;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_MASTER_VOLTAGE_MV:
        {
            *raw_value = config.master_voltage_mv;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_CONTROL_HZ:
        {
            *raw_value = config.control_hz;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_SENSOR_HZ:
        {
            *raw_value = config.sensor_hz;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_PHASE_MAP:
        {
            *raw_value = config.phase_map;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_AS5600_BOOT_CONF:
        {
            *raw_value = as5600_diagnostic.boot_config;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_AS5600_ACTIVE_CONF:
        {
            *raw_value = as5600_diagnostic.active_config;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_AS5600_READ_LAST_US:
        {
            *raw_value = as5600_diagnostic.read_last_us;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_AS5600_READ_MIN_US:
        {
            *raw_value = as5600_diagnostic.read_min_us;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_AS5600_READ_MAX_US:
        {
            *raw_value = as5600_diagnostic.read_max_us;
            break;
        }

        case CAN_PROTOCOL_FOC_CONFIG_AS5600_READ_AVERAGE_US:
        {
            *raw_value = as5600_diagnostic.read_average_us;
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
 * @brief 配置一个 CAN 标准帧过滤器。
 * @param bank 过滤器组编号，单位：无。
 * @param std_id 标准帧 ID，单位：无。
 * @return HAL_StatusTypeDef HAL 配置状态。
 */
static HAL_StatusTypeDef can_protocol_config_filter_bank(uint32_t bank, uint16_t std_id)
{
    CAN_FilterTypeDef filter = {0};
    HAL_StatusTypeDef status;

    filter.FilterBank = bank;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint16_t)(std_id << 5);
    filter.FilterIdLow = 0x0000u;
    filter.FilterMaskIdHigh = 0xFFE0u;
    filter.FilterMaskIdLow = 0x0006u;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    status = HAL_CAN_ConfigFilter(&hcan, &filter);
    if (status != HAL_OK)
    {
        printf("CAN filter fail: bank=%lu id=0x%03X status=%d err=0x%08lX\r\n",
               (unsigned long)bank,
               std_id,
               (int)status,
               (unsigned long)HAL_CAN_GetError(&hcan));
    }

    return status;
}

/**
 * @brief 根据当前节点 ID 更新 CAN 过滤器。
 * @return HAL_StatusTypeDef HAL 配置状态。
 */
static HAL_StatusTypeDef can_protocol_apply_filter(void)
{
    HAL_StatusTypeDef status;

    HAL_CAN_Stop(&hcan);

    if (can_protocol_config_filter_bank(0u, can_protocol_get_manage_command_std_id()) != HAL_OK)
    {
        AppError_Set(APP_ERROR_CAN_RECONFIGURE);
        return HAL_ERROR;
    }

    if (can_protocol_config_filter_bank(1u, can_protocol_get_command_std_id()) != HAL_OK)
    {
        AppError_Set(APP_ERROR_CAN_RECONFIGURE);
        return HAL_ERROR;
    }

    if (can_protocol_config_filter_bank(2u, can_protocol_get_ota_control_std_id()) != HAL_OK)
    {
        AppError_Set(APP_ERROR_CAN_RECONFIGURE);
        return HAL_ERROR;
    }

    status = HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
    if (status != HAL_OK)
    {
        printf("CAN notify fail: status=%d err=0x%08lX\r\n",
               (int)status,
               (unsigned long)HAL_CAN_GetError(&hcan));
        AppError_Set(APP_ERROR_CAN_RECONFIGURE);
        return HAL_ERROR;
    }

    status = HAL_CAN_Start(&hcan);
    if (status != HAL_OK)
    {
        printf("CAN start fail: status=%d err=0x%08lX state=%lu\r\n",
               (int)status,
               (unsigned long)HAL_CAN_GetError(&hcan),
               (unsigned long)HAL_CAN_GetState(&hcan));
        AppError_Set(APP_ERROR_CAN_RECONFIGURE);
        return HAL_ERROR;
    }

    printf("CAN filter ok: manage=0x%03X cmd=0x%03X ota=0x%03X state=%lu\r\n",
           can_protocol_get_manage_command_std_id(),
           can_protocol_get_command_std_id(),
           can_protocol_get_ota_control_std_id(),
           (unsigned long)HAL_CAN_GetState(&hcan));

    AppError_Clear(APP_ERROR_CAN_RECONFIGURE);
    return HAL_OK;
}

/**
 * @brief 根据参数编号设置速度环参数。
 * @param param_id 参数编号。
 * @param value 单精度浮点参数值。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_set_speed_pid_param(uint8_t param_id, float value)
{
    float p = 0.0f;
    float i = 0.0f;
    float d = 0.0f;
    float i_acc_max = 0.0f;
    float out_max = 0.0f;

    if (!isfinite(value))
    {
        return CAN_PROTOCOL_STATUS_INVALID_PARAM;
    }

    foc_speed_pid_get_full_param(&p, &i, &d, &i_acc_max, &out_max);

    switch (param_id)
    {
        case CAN_PROTOCOL_PID_PARAM_P:
        {
            p = value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I:
        {
            i = value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_D:
        {
            d = value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I_ACC_MAX:
        {
            if ((value < 0.0f) || (value > (float)OUT_MAX))
            {
                return CAN_PROTOCOL_STATUS_INVALID_PARAM;
            }

            i_acc_max = value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_OUT_MAX:
        {
            if ((value < 0.0f) || (value > (float)OUT_MAX))
            {
                return CAN_PROTOCOL_STATUS_INVALID_PARAM;
            }

            out_max = value;
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
 * @param value 参数值输出指针。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_get_speed_pid_param(uint8_t param_id, float *value)
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
            *value = p;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I:
        {
            *value = i;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_D:
        {
            *value = d;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I_ACC_MAX:
        {
            *value = i_acc_max;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_OUT_MAX:
        {
            *value = out_max;
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
 * @param value 单精度浮点参数值。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_set_position_pid_param(uint8_t param_id, float value)
{
    float p = 0.0f;
    float i = 0.0f;
    float d = 0.0f;
    float i_acc_max = 0.0f;
    float out_max = 0.0f;

    if (!isfinite(value))
    {
        return CAN_PROTOCOL_STATUS_INVALID_PARAM;
    }

    foc_position_pid_get_param(&p, &i, &d, &i_acc_max, &out_max);

    switch (param_id)
    {
        case CAN_PROTOCOL_PID_PARAM_P:
        {
            p = value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I:
        {
            i = value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_D:
        {
            d = value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I_ACC_MAX:
        {
            if ((value < 0.0f) || (value > (float)OUT_MAX))
            {
                return CAN_PROTOCOL_STATUS_INVALID_PARAM;
            }

            i_acc_max = value;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_OUT_MAX:
        {
            if ((value < 0.0f) || (value > (float)OUT_MAX))
            {
                return CAN_PROTOCOL_STATUS_INVALID_PARAM;
            }

            out_max = value;
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
 * @param value 参数值输出指针。
 * @return uint8_t 协议状态码。
 */
static uint8_t can_protocol_get_position_pid_param(uint8_t param_id, float *value)
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
            *value = p;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I:
        {
            *value = i;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_D:
        {
            *value = d;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_I_ACC_MAX:
        {
            *value = i_acc_max;
            break;
        }

        case CAN_PROTOCOL_PID_PARAM_OUT_MAX:
        {
            *value = out_max;
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
    uint8_t next_write_index;

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
 * @brief 刷新管理上报下次发送时间。
 * @param now_ms 当前系统时间，单位：毫秒。
 * @return void
 */
static void can_protocol_schedule_manage_report(uint32_t now_ms)
{
    can_protocol_next_manage_report_tick_ms = now_ms + (can_protocol_short_uid % CAN_PROTOCOL_MANAGE_REPORT_PERIOD_MS);
}

/**
 * @brief 发送未配置管理上报帧。
 * @return void
 */
static void can_protocol_send_manage_report(void)
{
    uint8_t report_data[8] = {0};

    report_data[0] = CAN_PROTOCOL_CMD_MANAGE_REPORT;
    report_data[1] = can_protocol_is_configured;
    report_data[2] = (uint8_t)can_protocol_node_id;
    can_protocol_encode_uint32(&report_data[3], can_protocol_short_uid);
    report_data[7] = 0u;

    if (can_protocol_send_frame(can_protocol_get_manage_report_std_id(), report_data, CAN_PROTOCOL_FULL_DLC) == HAL_OK)
    {
        can_protocol_manage_report_count++;
    }
}

/**
 * @brief 进入未配置低待机状态。
 * @return void
 */
static void can_protocol_enter_unconfigured_state(void)
{
    can_protocol_node_id = CAN_PROTOCOL_MANAGE_NODE_ID;
    can_protocol_is_configured = USER_INFO_CAN_CONFIGURED_NO;
    can_protocol_next_unconfigured_log_tick_ms = HAL_GetTick() + CAN_PROTOCOL_UNCONFIGURED_LOG_PERIOD_MS;
    if (can_protocol_foc_available != 0u)
    {
        AppLight_SetMode(APP_LIGHT_MODE_UNCONFIGURED);
    }
    else
    {
        AppLight_SetMode(APP_LIGHT_MODE_ERROR);
    }

    foc_output_enable(0u);
    foc_set_state(&foc, Foc_Shutdown);
    printf("CAN node unconfigured: manage_id=0x%02X uid=0x%08lX\r\n",
           CAN_PROTOCOL_MANAGE_NODE_ID,
           (unsigned long)can_protocol_short_uid);
}

/**
 * @brief 进入已配置运行状态。
 * @param node_id 已配置业务节点 ID，单位：无。
 * @return void
 */
static void can_protocol_enter_configured_state(uint16_t node_id)
{
    can_protocol_node_id = node_id;
    can_protocol_is_configured = USER_INFO_CAN_CONFIGURED_YES;
    can_protocol_next_unconfigured_log_tick_ms = 0u;
    if (can_protocol_foc_available != 0u)
    {
        AppLight_SetMode(APP_LIGHT_MODE_RUNNING);
        foc_output_enable(1u);
        foc_set_state(&foc, Foc_Working);
    }
    else
    {
        AppLight_SetMode(APP_LIGHT_MODE_ERROR);
        foc_output_enable(0u);
        foc_set_state(&foc, Foc_Shutdown);
    }
    printf("CAN node configured: node=0x%02X uid=0x%08lX report=%u period=%lu ms\r\n",
           can_protocol_node_id,
           (unsigned long)can_protocol_short_uid,
           can_protocol_report_enabled,
           (unsigned long)can_protocol_report_period_ms);
}

/**
 * @brief 判断管理命令 UID 是否匹配本机。
 * @param rx_data 接收数据缓冲区。
 * @return uint8_t 匹配返回 1，否则返回 0。
 */
static uint8_t can_protocol_manage_uid_match(const uint8_t *rx_data)
{
    uint32_t target_uid;

    target_uid = can_protocol_decode_uint32(&rx_data[3]);
    if (target_uid == can_protocol_short_uid)
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief 处理管理识别灯效命令。
 * @param rxframe 接收帧头。
 * @param rx_data 接收数据缓冲区。
 * @return void
 */
static void can_protocol_handle_manage_identify(CAN_RxHeaderTypeDef rxframe, const uint8_t *rx_data)
{
    uint8_t ack_payload[6] = {0};
    uint8_t duration_s;
    uint16_t duration_ms;

    if (rxframe.DLC < 7u)
    {
        return;
    }

    if (can_protocol_manage_uid_match(rx_data) == 0u)
    {
        return;
    }

    duration_s = rx_data[2];
    if (duration_s == 0u)
    {
        duration_s = CAN_PROTOCOL_MANAGE_IDENTIFY_DEFAULT_S;
    }

    if (duration_s > CAN_PROTOCOL_MANAGE_IDENTIFY_MAX_S)
    {
        duration_s = CAN_PROTOCOL_MANAGE_IDENTIFY_MAX_S;
    }

    duration_ms = (uint16_t)((uint16_t)duration_s * 1000u);
    AppLight_StartIdentify(rx_data[1], duration_ms);

    ack_payload[0] = rx_data[1];
    ack_payload[1] = duration_s;
    can_protocol_encode_uint32(&ack_payload[2], can_protocol_short_uid);
    can_protocol_send_manage_ack(CAN_PROTOCOL_CMD_MANAGE_IDENTIFY,
                                 CAN_PROTOCOL_STATUS_OK,
                                 ack_payload,
                                 6u);
}

/**
 * @brief 处理管理配置 CAN 节点 ID 命令。
 * @param rxframe 接收帧头。
 * @param rx_data 接收数据缓冲区。
 * @return void
 */
static void can_protocol_handle_manage_set_can_id(CAN_RxHeaderTypeDef rxframe, const uint8_t *rx_data)
{
    uint8_t ack_payload[6] = {0};
    uint16_t new_node_id;

    if (rxframe.DLC < 7u)
    {
        return;
    }

    if (can_protocol_manage_uid_match(rx_data) == 0u)
    {
        return;
    }

    if (can_protocol_is_valid_configured_node_id(rx_data[1]) == 0u)
    {
        can_protocol_send_manage_ack(CAN_PROTOCOL_CMD_MANAGE_SET_CAN_ID,
                                     CAN_PROTOCOL_STATUS_INVALID_PARAM,
                                     NULL,
                                     0u);
        return;
    }

    new_node_id = (uint16_t)rx_data[1];
    ack_payload[0] = (uint8_t)new_node_id;
    can_protocol_encode_uint32(&ack_payload[1], can_protocol_short_uid);
    can_protocol_send_manage_ack(CAN_PROTOCOL_CMD_MANAGE_SET_CAN_ID,
                                 CAN_PROTOCOL_STATUS_OK,
                                 ack_payload,
                                 5u);

    can_protocol_store_node_id(new_node_id);
    can_protocol_enter_configured_state(new_node_id);

    if (can_protocol_apply_filter() != HAL_OK)
    {
        can_protocol_send_manage_ack(CAN_PROTOCOL_CMD_MANAGE_SET_CAN_ID,
                                     CAN_PROTOCOL_STATUS_CAN_ERROR,
                                     NULL,
                                     0u);
    }
}

/**
 * @brief 处理管理命令帧。
 * @param rxframe 接收帧头。
 * @param rx_data 接收数据缓冲区。
 * @return void
 */
static void can_protocol_handle_manage_frame(CAN_RxHeaderTypeDef rxframe, const uint8_t *rx_data)
{
    if (rxframe.DLC == 0u)
    {
        return;
    }

    switch (rx_data[0])
    {
        case CAN_PROTOCOL_CMD_MANAGE_IDENTIFY:
        {
            can_protocol_handle_manage_identify(rxframe, rx_data);
            return;
        }

        case CAN_PROTOCOL_CMD_MANAGE_SET_CAN_ID:
        {
            can_protocol_handle_manage_set_can_id(rxframe, rx_data);
            return;
        }

        default:
        {
            return;
        }
    }
}

/**
 * @brief 初始化 CAN 协议层并配置过滤器。
 * @param foc_available FOC 可用标志，0 表示不可用，非 0 表示可用。
 * @return void
 */
void can_protocol_init(uint8_t foc_available)
{
    recoder_data data = {0};
    uint32_t now_ms;

    can_protocol_foc_available = foc_available;
    can_protocol_short_uid = ChipUid_GetShortId();
    printf("CAN protocol init: app=%s uid=0x%08lX\r\n",
           AppVersion_GetString(),
           (unsigned long)can_protocol_short_uid);

    if (Load_Recoder(&data) != 0u)
    {
        can_protocol_load_report_config(&data);

        if ((data.can_configured == USER_INFO_CAN_CONFIGURED_YES) &&
            (can_protocol_is_valid_configured_node_id(data.can_id) != 0u))
        {
            can_protocol_enter_configured_state((uint16_t)data.can_id);
        }
        else
        {
            can_protocol_enter_unconfigured_state();
        }
    }
    else
    {
        can_protocol_load_report_config(NULL);
        can_protocol_enter_unconfigured_state();
    }

    now_ms = HAL_GetTick();
    can_protocol_schedule_manage_report(now_ms);
    can_protocol_next_error_report_tick_ms = now_ms;
    if (can_protocol_apply_filter() != HAL_OK)
    {
        printf("CAN protocol init filter failed\r\n");
    }
}

/**
 * @brief 轮询 CAN 协议层周期任务。
 * @return void
 */
void can_protocol_poll(void)
{
    uint32_t now_ms;

    now_ms = HAL_GetTick();
    if ((int32_t)(now_ms - can_protocol_next_error_report_tick_ms) >= 0)
    {
        can_protocol_send_error_report();
        can_protocol_next_error_report_tick_ms = now_ms + CAN_PROTOCOL_ERROR_REPORT_PERIOD_MS;
    }

    if (can_protocol_is_configured == USER_INFO_CAN_CONFIGURED_YES)
    {
        return;
    }

    if ((int32_t)(now_ms - can_protocol_next_unconfigured_log_tick_ms) >= 0)
    {
        printf("App unconfigured: uid=0x%08lX manage=0x%02X tx_ok=%lu tx_fail=%lu rx=%lu report=%lu\r\n",
               (unsigned long)can_protocol_short_uid,
               CAN_PROTOCOL_MANAGE_NODE_ID,
               (unsigned long)can_protocol_tx_ok_count,
               (unsigned long)can_protocol_tx_fail_count,
               (unsigned long)can_protocol_rx_count,
               (unsigned long)can_protocol_manage_report_count);
        can_protocol_next_unconfigured_log_tick_ms = now_ms + CAN_PROTOCOL_UNCONFIGURED_LOG_PERIOD_MS;
    }

    if ((int32_t)(now_ms - can_protocol_next_manage_report_tick_ms) < 0)
    {
        return;
    }

    can_protocol_send_manage_report();
    can_protocol_next_manage_report_tick_ms = now_ms + CAN_PROTOCOL_MANAGE_REPORT_PERIOD_MS;
}

/**
 * @brief 获取当前协议层使用的节点 ID。
 * @return uint16_t 当前节点 ID，单位：无。
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
    uint32_t now_ms;

    if (can_protocol_is_configured == USER_INFO_CAN_CONFIGURED_NO)
    {
        return;
    }

    if (can_protocol_report_enabled == USER_INFO_REPORT_DISABLED)
    {
        return;
    }

    can_protocol_report_speed_pid_debug();

    now_ms = HAL_GetTick();
    if ((int32_t)(now_ms - can_protocol_next_motor_report_tick_ms) < 0)
    {
        return;
    }

    can_protocol_next_motor_report_tick_ms = now_ms + can_protocol_report_period_ms;

    can_protocol_encode_int32(&report_data[0], foc_get_angle(&foc));
    can_protocol_encode_int32(&report_data[4], foc_get_speed_estimate());
    can_protocol_send_frame(can_protocol_get_report_std_id(), report_data, CAN_PROTOCOL_FULL_DLC);
}

/**
 * @brief 将单精度浮点数按 IEEE 754 小端格式写入缓冲区。
 * @param data 目标缓冲区指针。
 * @param value 待写入的单精度浮点数。
 * @return void
 */
static void can_protocol_encode_float32(uint8_t *data, float value)
{
    uint32_t raw_value = 0u;

    memcpy(&raw_value, &value, sizeof(raw_value));
    can_protocol_encode_uint32(data, raw_value);
}

/**
 * @brief 从 IEEE 754 小端缓冲区读取单精度浮点数。
 * @param data 源缓冲区指针。
 * @return float 解析后的单精度浮点数。
 */
static float can_protocol_decode_float32(const uint8_t *data)
{
    uint32_t raw_value;
    float value = 0.0f;

    raw_value = can_protocol_decode_uint32(data);
    memcpy(&value, &raw_value, sizeof(value));
    return value;
}

/**
 * @brief 周期上报速度环 PID 运行时诊断数据。
 * @return void
 * @note 诊断帧默认随主动上报启用，固定周期为 10ms。
 */
static void can_protocol_report_speed_pid_debug(void)
{
    uint8_t report_data[8] = {0};
    uint32_t now_ms;

    now_ms = HAL_GetTick();
    if ((int32_t)(now_ms - can_protocol_next_pid_debug_report_tick_ms) < 0)
    {
        return;
    }

    can_protocol_next_pid_debug_report_tick_ms = now_ms
                                                 + CAN_PROTOCOL_PID_DEBUG_FRAME_PERIOD_MS;

    if (can_protocol_pid_debug_frame_index == 0u)
    {
        foc_speed_pid_get_runtime(&can_protocol_pid_debug_runtime);
        can_protocol_encode_float32(&report_data[0], can_protocol_pid_debug_runtime.target);
        can_protocol_encode_float32(&report_data[4], can_protocol_pid_debug_runtime.feedback);
        can_protocol_send_frame(CAN_PROTOCOL_PID_DEBUG_STATE_BASE_ID + can_protocol_node_id,
                                report_data,
                                CAN_PROTOCOL_FULL_DLC);
    }
    else
    {
        if (can_protocol_pid_debug_frame_index == 1u)
        {
            can_protocol_encode_float32(&report_data[0], can_protocol_pid_debug_runtime.error);
            can_protocol_encode_float32(&report_data[4], can_protocol_pid_debug_runtime.error_delta);
            can_protocol_send_frame(CAN_PROTOCOL_PID_DEBUG_ERROR_BASE_ID + can_protocol_node_id,
                                    report_data,
                                    CAN_PROTOCOL_FULL_DLC);
        }
        else
        {
            if (can_protocol_pid_debug_frame_index == 2u)
            {
                can_protocol_encode_float32(&report_data[0], can_protocol_pid_debug_runtime.integral_acc);
                can_protocol_encode_float32(&report_data[4], can_protocol_pid_debug_runtime.integral_output);
                can_protocol_send_frame(CAN_PROTOCOL_PID_DEBUG_INTEGRAL_BASE_ID + can_protocol_node_id,
                                        report_data,
                                        CAN_PROTOCOL_FULL_DLC);
            }
            else
            {
                can_protocol_encode_float32(&report_data[0], can_protocol_pid_debug_runtime.output);
                can_protocol_encode_int32(&report_data[4], can_protocol_pid_debug_runtime.q_target);
                can_protocol_send_frame(CAN_PROTOCOL_PID_DEBUG_OUTPUT_BASE_ID + can_protocol_node_id,
                                        report_data,
                                        CAN_PROTOCOL_FULL_DLC);
            }
        }
    }

    can_protocol_pid_debug_frame_index++;
    if (can_protocol_pid_debug_frame_index >= CAN_PROTOCOL_PID_DEBUG_FRAME_COUNT)
    {
        can_protocol_pid_debug_frame_index = 0u;
    }
}

/**
 * @brief 设置 FOC 当前是否可用。
 * @param foc_available FOC 可用标志，0 表示不可用，非 0 表示可用。
 * @return void
 */
void can_protocol_set_foc_available(uint8_t foc_available)
{
    can_protocol_foc_available = foc_available;
    if (can_protocol_foc_available == 0u)
    {
        foc_output_enable(0u);
        foc_set_state(&foc, Foc_Shutdown);
        AppLight_SetMode(APP_LIGHT_MODE_ERROR);
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

    can_protocol_upload_foc_trace();
}

/**
 * @brief 判断当前是否存在待处理的 CAN 协议数据。
 * @return int 存在待处理数据返回 1，否则返回 0。
 */
int can_protocol_has_pending(void)
{
    if (can_protocol_rx_read_index != can_protocol_rx_write_index)
    {
        return 1;
    }

    if (can_protocol_rx_overflow_flag != 0u)
    {
        return 1;
    }

    if (can_protocol_foc_trace_upload_active != 0u)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 判断命令是否依赖可用的 FOC 控制链路。
 * @param command CAN 命令码。
 * @return uint8_t 依赖 FOC 返回 1，否则返回 0。
 */
static uint8_t can_protocol_command_requires_foc(uint8_t command)
{
    switch (command)
    {
        case CAN_PROTOCOL_CMD_SET_MODE:
        case CAN_PROTOCOL_CMD_SET_CURRENT_TARGET:
        case CAN_PROTOCOL_CMD_SET_SPEED_TARGET:
        case CAN_PROTOCOL_CMD_SET_POSITION_TARGET:
        case CAN_PROTOCOL_CMD_ZERO_CALIBRATE:
        {
            return 1u;
        }

        default:
        {
            return 0u;
        }
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
    float pid_value = 0.0f;
    uint8_t status = CAN_PROTOCOL_STATUS_OK;

    if (rx_data == NULL)
    {
        return;
    }

    if (rxframe.StdId == can_protocol_get_manage_command_std_id())
    {
        can_protocol_handle_manage_frame(rxframe, rx_data);
        return;
    }

    if (can_protocol_is_configured == USER_INFO_CAN_CONFIGURED_NO)
    {
        return;
    }

    if ((rxframe.StdId == can_protocol_get_ota_control_std_id()) &&
        (can_protocol_is_ota_start_request(rxframe, rx_data) != 0u))
    {
        can_protocol_enter_boot_ota(CAN_PROTOCOL_CMD_ENTER_BOOT_OTA);
        return;
    }

    if ((rxframe.StdId != can_protocol_get_command_std_id()) &&
        (rxframe.StdId != can_protocol_get_ota_control_std_id()))
    {
        return;
    }

    if (rxframe.DLC == 0u)
    {
        can_protocol_send_ack(0u, CAN_PROTOCOL_STATUS_INVALID_PARAM, NULL, 0u);
        return;
    }

    if ((can_protocol_foc_available == 0u) &&
        (can_protocol_command_requires_foc(rx_data[0]) != 0u))
    {
        can_protocol_send_ack(rx_data[0], CAN_PROTOCOL_STATUS_FOC_UNAVAILABLE, NULL, 0u);
        return;
    }

    switch (rx_data[0])
    {
        case CAN_PROTOCOL_CMD_SET_MODE:
        {
            if (rxframe.DLC < 3u)
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

            if (rx_data[1] == 0u)
            {
                foc_current_set_target(0);
                foc_set_target(0, 0, 0);
                foc_set_state(&foc, Foc_Shutdown);
                foc_output_enable(0U);
            }
            else
            {
                foc_output_enable(1U);
                foc_set_state(&foc, Foc_Working);
            }

            ack_payload[0] = rx_data[1];
            ack_payload[1] = rx_data[2];
            can_protocol_send_ack(rx_data[0], status, ack_payload, 2u);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_SPEED_TARGET:
        {
            if (rxframe.DLC < 5u)
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
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4u);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_POSITION_TARGET:
        {
            if (rxframe.DLC < 5u)
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
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4u);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_CURRENT_TARGET:
        {
            if (rxframe.DLC < 5u)
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
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4u);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_SPEED_PID_PARAM:
        {
            if (rxframe.DLC < 6u)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            pid_value = can_protocol_decode_float32(&rx_data[2]);
            status = can_protocol_set_speed_pid_param(rx_data[1], pid_value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_float32(&ack_payload[1], pid_value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5u);
            return;
        }

        case CAN_PROTOCOL_CMD_GET_SPEED_PID_PARAM:
        {
            if (rxframe.DLC < 2u)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            status = can_protocol_get_speed_pid_param(rx_data[1], &pid_value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_float32(&ack_payload[1], pid_value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5u);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_POSITION_PID_PARAM:
        {
            if (rxframe.DLC < 6u)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            pid_value = can_protocol_decode_float32(&rx_data[2]);
            status = can_protocol_set_position_pid_param(rx_data[1], pid_value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_float32(&ack_payload[1], pid_value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5u);
            return;
        }

        case CAN_PROTOCOL_CMD_GET_POSITION_PID_PARAM:
        {
            if (rxframe.DLC < 2u)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            status = can_protocol_get_position_pid_param(rx_data[1], &pid_value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_float32(&ack_payload[1], pid_value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5u);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_CAN_ID:
        {
            uint16_t new_node_id;

            if (rxframe.DLC < 2u)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            if (can_protocol_is_valid_configured_node_id(rx_data[1]) == 0u)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            new_node_id = (uint16_t)rx_data[1];
            ack_payload[0] = (uint8_t)new_node_id;
            can_protocol_send_ack(rx_data[0], status, ack_payload, 1u);

            can_protocol_store_node_id(new_node_id);
            can_protocol_enter_configured_state(new_node_id);

            if (can_protocol_apply_filter() != HAL_OK)
            {
                can_protocol_send_ack(rx_data[0], CAN_PROTOCOL_STATUS_CAN_ERROR, NULL, 0u);
            }

            return;
        }

        case CAN_PROTOCOL_CMD_GET_CAN_ID:
        {
            ack_payload[0] = (uint8_t)can_protocol_node_id;
            can_protocol_send_ack(rx_data[0], status, ack_payload, 1u);
            return;
        }

        case CAN_PROTOCOL_CMD_GET_APP_VERSION:
        {
            app_version_t version;
            const char *version_string;

            version_string = AppVersion_GetString();
            if ((version_string == NULL) || (version_string[0] == '\0'))
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            version = AppVersion_Get();
            ack_payload[0] = version.major;
            ack_payload[1] = version.minor;
            ack_payload[2] = version.patch;
            can_protocol_send_ack(rx_data[0], status, ack_payload, 3u);
            return;
        }

        case CAN_PROTOCOL_CMD_SET_REPORT_CONFIG:
        {
            uint32_t report_enabled;
            uint32_t report_period_ms;

            if (rxframe.DLC < CAN_PROTOCOL_REPORT_CONFIG_DLC)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            if (rx_data[1] > USER_INFO_REPORT_ENABLED)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            report_enabled = rx_data[1];
            report_period_ms = can_protocol_decode_uint16(&rx_data[2]);

            if ((report_period_ms < USER_INFO_MIN_REPORT_PERIOD_MS) ||
                (report_period_ms > USER_INFO_MAX_REPORT_PERIOD_MS))
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            status = can_protocol_store_report_config(report_enabled, report_period_ms);
            ack_payload[0] = can_protocol_report_enabled;
            can_protocol_encode_uint16(&ack_payload[1], (uint16_t)can_protocol_report_period_ms);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 3u);
            return;
        }

        case CAN_PROTOCOL_CMD_GET_REPORT_CONFIG:
        {
            ack_payload[0] = can_protocol_report_enabled;
            can_protocol_encode_uint16(&ack_payload[1], (uint16_t)can_protocol_report_period_ms);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 3u);
            return;
        }

        case CAN_PROTOCOL_CMD_RESET_CAN_CONFIG:
        {
            if (rxframe.DLC < CAN_PROTOCOL_RESET_CAN_CONFIG_DLC)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            if (rx_data[1] != CAN_PROTOCOL_RESET_CAN_CONFIG_CONFIRM_CODE)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            status = can_protocol_reset_can_config();
            can_protocol_send_ack(rx_data[0], status, NULL, 0u);
            if (status == CAN_PROTOCOL_STATUS_OK)
            {
                HAL_Delay(CAN_PROTOCOL_RESET_DELAY_MS);
                NVIC_SystemReset();
            }

            return;
        }

        case CAN_PROTOCOL_CMD_SET_FOC_CONFIG:
        {
            uint32_t raw_value;

            if (rxframe.DLC < 6u)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            raw_value = can_protocol_decode_uint32(&rx_data[2]);
            status = can_protocol_set_foc_config_param(rx_data[1], raw_value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_uint32(&ack_payload[1], raw_value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5u);
            return;
        }

        case CAN_PROTOCOL_CMD_GET_FOC_CONFIG:
        {
            uint32_t raw_value = 0u;

            if (rxframe.DLC < 2u)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            status = can_protocol_get_foc_config_param(rx_data[1], &raw_value);
            ack_payload[0] = rx_data[1];
            can_protocol_encode_uint32(&ack_payload[1], raw_value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 5u);
            return;
        }

        case CAN_PROTOCOL_CMD_ZERO_CALIBRATE:
        {
            value = foc_zero_angle_reset();
            can_protocol_store_zero_angle(value);
            can_protocol_encode_int32(ack_payload, value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4u);
            return;
        }

        case CAN_PROTOCOL_CMD_GET_ZERO:
        {
            value = foc_get_zero_angle();
            can_protocol_encode_int32(ack_payload, value);
            can_protocol_send_ack(rx_data[0], status, ack_payload, 4u);
            return;
        }

        case CAN_PROTOCOL_CMD_ENTER_BOOT_OTA:
        {
            can_protocol_enter_boot_ota(rx_data[0]);
            return;
        }

        case CAN_PROTOCOL_CMD_FOC_TRACE:
        {
            uint8_t action;

            if (rxframe.DLC < 2u)
            {
                status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                break;
            }

            action = rx_data[1];
            if (action == CAN_PROTOCOL_FOC_TRACE_ACTION_START)
            {
                can_protocol_foc_trace_upload_active = 0u;
                foc_trace_start();
            }
            else
            {
                if (action == CAN_PROTOCOL_FOC_TRACE_ACTION_STOP)
                {
                    foc_trace_stop();
                }
                else
                {
                    if (action == CAN_PROTOCOL_FOC_TRACE_ACTION_UPLOAD)
                    {
                        foc_trace_stop();

                        if (foc_trace_get_count() == 0u)
                        {
                            status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                        }
                        else
                        {
                            can_protocol_foc_trace_upload_index = 0u;
                            can_protocol_foc_trace_upload_part = 0u;
                            can_protocol_foc_trace_upload_active = 1u;
                        }
                    }
                    else
                    {
                        status = CAN_PROTOCOL_STATUS_INVALID_PARAM;
                    }
                }
            }

            ack_payload[0] = action;
            ack_payload[1] = foc_trace_get_state();
            can_protocol_encode_uint16(&ack_payload[2], foc_trace_get_count());
            can_protocol_encode_uint16(&ack_payload[4], foc_trace_get_sample_hz());
            can_protocol_send_ack(rx_data[0], status, ack_payload, 6u);
            return;
        }

        default:
        {
            status = CAN_PROTOCOL_STATUS_INVALID_CMD;
            break;
        }
    }

    can_protocol_send_ack(rx_data[0], status, NULL, 0u);
}

/**
 * @brief 在主循环中分帧上传已冻结的 FOC 高速记录。
 * @return void
 */
static void can_protocol_upload_foc_trace(void)
{
    uint8_t report_data[8] = {0};
    uint16_t std_id;
    const uint8_t *sample_bytes;
    foc_trace_sample_t sample;

    if (can_protocol_foc_trace_upload_active == 0u)
    {
        return;
    }

    if (can_protocol_foc_trace_upload_index >= foc_trace_get_count())
    {
        can_protocol_foc_trace_upload_active = 0u;
        return;
    }

    if (foc_trace_get_sample(can_protocol_foc_trace_upload_index, &sample) == 0u)
    {
        can_protocol_foc_trace_upload_active = 0u;
        return;
    }

    sample_bytes = (const uint8_t *)&sample;
    can_protocol_encode_uint16(&report_data[0], can_protocol_foc_trace_upload_index);
    memcpy(&report_data[2],
           &sample_bytes[can_protocol_foc_trace_upload_part
                         * CAN_PROTOCOL_FOC_TRACE_PART_DATA_LENGTH],
           CAN_PROTOCOL_FOC_TRACE_PART_DATA_LENGTH);

    if (can_protocol_foc_trace_upload_part == 0u)
    {
        std_id = CAN_PROTOCOL_FOC_TRACE_PART0_BASE_ID + can_protocol_node_id;
    }
    else
    {
        if (can_protocol_foc_trace_upload_part == 1u)
        {
            std_id = CAN_PROTOCOL_FOC_TRACE_PART1_BASE_ID + can_protocol_node_id;
        }
        else
        {
            std_id = CAN_PROTOCOL_FOC_TRACE_PART2_BASE_ID + can_protocol_node_id;
        }
    }

    if (can_protocol_send_frame(std_id, report_data, CAN_PROTOCOL_FULL_DLC) != HAL_OK)
    {
        return;
    }

    can_protocol_foc_trace_upload_part++;
    if (can_protocol_foc_trace_upload_part >= CAN_PROTOCOL_FOC_TRACE_PART_COUNT)
    {
        can_protocol_foc_trace_upload_part = 0u;
        can_protocol_foc_trace_upload_index++;
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
        can_protocol_rx_count++;
        can_protocol_rx_queue_push(&rx_header, rx_data);
    }
}
