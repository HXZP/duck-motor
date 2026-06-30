#ifndef OTA_INFO_H
#define OTA_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "common/flash_layout.h"
#include "common/user_info.h"

#define OTA_INFO_FLASH_ADDRESS         FLASH_LAYOUT_OTA_INFO_ADDRESS
#define OTA_INFO_FLASH_PAGE_SIZE       FLASH_LAYOUT_FLASH_PAGE_SIZE
#define OTA_INFO_MAGIC                 0x4F544149u
#define OTA_INFO_VERSION               1u
#define OTA_INFO_FLAG_APP              USER_INFO_OTA_FLAG_APP
#define OTA_INFO_FLAG_BOOT             USER_INFO_OTA_FLAG_BOOT

/**
 * @brief OTA 持久化业务数据。
 */
typedef struct
{
    uint32_t ota_flag;       /**< OTA 标志位，0 表示允许跳转 App，1 表示停留 Boot。 */
    uint32_t app_size;       /**< App 镜像长度，单位：字节。 */
    uint16_t app_crc16;      /**< App 镜像 CRC16-CCITT 校验值，单位：无。 */
    uint16_t reserved;       /**< 保留字段，单位：无。 */
} ota_info_data_t;

/**
 * @brief OTA 信息 Flash 存储记录。
 */
typedef struct
{
    uint32_t magic;          /**< 记录魔数，单位：无。 */
    uint32_t version;        /**< 记录格式版本，单位：无。 */
    ota_info_data_t data;    /**< OTA 业务数据。 */
    uint32_t crc32;          /**< CRC32 校验值，单位：无。 */
} ota_info_record_t;

/**
 * @brief 获取默认 OTA 信息。
 * @param info OTA 信息输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_GetDefault(ota_info_data_t *info);

/**
 * @brief 读取 OTA 信息。
 * @param info OTA 信息输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_Load(ota_info_data_t *info);

/**
 * @brief 保存 OTA 信息。
 * @param info 待保存的 OTA 信息。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_Save(const ota_info_data_t *info);

/**
 * @brief 读取 OTA 标志位。
 * @param ota_flag OTA 标志位输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_LoadFlag(uint32_t *ota_flag);

/**
 * @brief 保存 OTA 标志位。
 * @param ota_flag OTA 标志位。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_SaveFlag(uint32_t ota_flag);

/**
 * @brief 保存 App 校验信息并允许跳转 App。
 * @param app_size App 镜像长度，单位：字节。
 * @param app_crc16 App 镜像 CRC16-CCITT 校验值，单位：无。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_SaveAppValid(uint32_t app_size, uint16_t app_crc16);

#ifdef __cplusplus
}
#endif

#endif /* OTA_INFO_H */
