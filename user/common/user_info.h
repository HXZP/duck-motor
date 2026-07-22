#ifndef USER_INFO_H
#define USER_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "common/flash_layout.h"

#define USER_INFO_OK                    0
#define USER_INFO_ERR                  -1
#define USER_INFO_ERR_PARAM            -2
#define USER_INFO_ERR_VERIFY           -3

#define USER_INFO_FLASH_ADDRESS         FLASH_LAYOUT_USER_INFO_ADDRESS
#define USER_INFO_FLASH_PAGE_SIZE       FLASH_LAYOUT_FLASH_PAGE_SIZE
#define USER_INFO_MAGIC                 0xA5A55A5Au
#define USER_INFO_VERSION               3u

#define USER_INFO_MANAGE_CAN_ID         0x7Eu
#define USER_INFO_DEFAULT_CAN_ID        USER_INFO_MANAGE_CAN_ID
#define USER_INFO_CAN_ID_MIN            0x01u
#define USER_INFO_CAN_ID_MAX            0x7Fu
#define USER_INFO_CAN_CONFIGURED_NO     0u
#define USER_INFO_CAN_CONFIGURED_YES    1u
#define USER_INFO_REPORT_DISABLED       0u
#define USER_INFO_REPORT_ENABLED        1u
#define USER_INFO_DEFAULT_REPORT_ENABLE USER_INFO_REPORT_DISABLED
#define USER_INFO_DEFAULT_REPORT_PERIOD_MS 10u
#define USER_INFO_MIN_REPORT_PERIOD_MS  1u
#define USER_INFO_MAX_REPORT_PERIOD_MS  60000u
#define USER_INFO_DEFAULT_POLE_PAIRS    7u
#define USER_INFO_MIN_POLE_PAIRS        1u
#define USER_INFO_MAX_POLE_PAIRS        32u
#define USER_INFO_DEFAULT_MASTER_VOLTAGE_MV 12000u
#define USER_INFO_MIN_MASTER_VOLTAGE_MV 1u
#define USER_INFO_MAX_MASTER_VOLTAGE_MV 60000u
#define USER_INFO_DEFAULT_CONTROL_HZ    2000u
#define USER_INFO_DEFAULT_SENSOR_HZ     2000u
#define USER_INFO_MIN_LOOP_HZ           1u
#define USER_INFO_MAX_LOOP_HZ           4000u
#define USER_INFO_DEFAULT_PHASE_MAP     0u
#define USER_INFO_MIN_PHASE_MAP         0u
#define USER_INFO_MAX_PHASE_MAP         1u

#define USER_INFO_OTA_FLAG_APP          0u
#define USER_INFO_OTA_FLAG_BOOT         1u

/**
 * @brief FOC 持久化配置数据。
 */
typedef struct
{
    uint32_t pole_pairs;        /**< 电机极对数，单位：个。 */
    uint32_t master_voltage_mv; /**< 母线电压，单位：毫伏。 */
    uint32_t control_hz;        /**< FOC 控制频率，单位：Hz。 */
    uint32_t sensor_hz;         /**< 传感器采样频率，单位：Hz。 */
    uint32_t phase_map;         /**< 三相输出映射编号，单位：无。 */
} user_info_foc_config_t;

/**
 * @brief 用户信息业务数据。
 *
 * 该结构体保存到 Flash 最后一页，用于在 App 和 Boot 之间共享参数。
 */
typedef struct
{
    int32_t calibration_angle;      /**< 校准角度，单位：内部角度计数。 */
    uint32_t can_id;                /**< CAN 节点 ID，单位：无。 */
    uint32_t ota;                   /**< 旧版 OTA 标志占位，单位：无。 */
    uint32_t can_configured;        /**< CAN 节点 ID 配置状态，单位：无。 */
    uint32_t report_enabled;        /**< 电机主动上报使能状态，单位：无。 */
    uint32_t report_period_ms;      /**< 电机主动上报周期，单位：毫秒。 */
} user_info_data_t;

/**
 * @brief 用户信息 Flash 存储记录。
 */
typedef struct
{
    uint32_t magic;                 /**< 记录魔数，单位：无。 */
    uint32_t version;               /**< 记录格式版本，单位：无。 */
    user_info_data_t data;          /**< 用户信息业务数据。 */
    uint32_t crc32;                 /**< CRC32 校验值，单位：无。 */
} user_info_record_t;

/**
 * @brief 读取用户信息。
 * @param info 用户信息输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_Load(user_info_data_t *info);

/**
 * @brief 保存用户信息。
 * @param info 待保存的用户信息。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_Save(const user_info_data_t *info);

/**
 * @brief 获取默认用户信息。
 * @param info 用户信息输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_GetDefault(user_info_data_t *info);

/**
 * @brief 读取 OTA 标志位。
 * @param ota_flag OTA 标志位输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_LoadOtaFlag(uint32_t *ota_flag);

/**
 * @brief 保存 OTA 标志位。
 * @param ota_flag OTA 标志位。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_SaveOtaFlag(uint32_t ota_flag);

/**
 * @brief 计算 CRC32 校验值。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return uint32_t CRC32 校验值，单位：无。
 */
uint32_t UserInfo_CalcCrc32(const uint8_t *data, uint32_t len);

/**
 * @brief 获取默认 FOC 配置。
 * @param config FOC 配置输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_GetDefaultFocConfig(user_info_foc_config_t *config);

/**
 * @brief 校验 FOC 配置是否在数值合法范围内。
 * @param config 待校验的 FOC 配置。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_ValidateFocConfig(const user_info_foc_config_t *config);

/**
 * @brief 归一化 FOC 配置。
 * @param config 待归一化的 FOC 配置。
 * @return void
 */
void UserInfo_NormalizeFocConfig(user_info_foc_config_t *config);

/**
 * @brief 更新用户信息页尾部扩展数据。
 * @param page_offset 页内偏移，单位：字节。
 * @param data 扩展数据缓冲区。
 * @param len 扩展数据长度，单位：字节。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_SavePageTail(uint32_t page_offset, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* USER_INFO_H */
