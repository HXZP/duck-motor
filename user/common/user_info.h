#ifndef USER_INFO_H
#define USER_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define USER_INFO_OK                    0
#define USER_INFO_ERR                  -1
#define USER_INFO_ERR_PARAM            -2
#define USER_INFO_ERR_VERIFY           -3

#define USER_INFO_FLASH_ADDRESS         0x0800FC00u
#define USER_INFO_FLASH_PAGE_SIZE       1024u
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
#define USER_INFO_MIN_REPORT_PERIOD_MS  10u
#define USER_INFO_MAX_REPORT_PERIOD_MS  60000u

#define USER_INFO_OTA_FLAG_APP          0u
#define USER_INFO_OTA_FLAG_BOOT         1u

/**
 * @brief 用户信息业务数据。
 *
 * 该结构体保存到 Flash 最后一页，用于在 App 和 Boot 之间共享参数。
 */
typedef struct
{
    int32_t calibration_angle;      /**< 校准角度，单位：内部角度计数。 */
    uint32_t can_id;                /**< CAN 节点 ID，单位：无。 */
    uint32_t ota;                   /**< OTA 标志位，0 表示允许跳转 App，非 0 表示停留 Boot。 */
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

#ifdef __cplusplus
}
#endif

#endif /* USER_INFO_H */
