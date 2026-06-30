#include "app/flash.h"

#include "common/ota_info.h"

#include <string.h>

#define FLASH_FOC_CONFIG_MAGIC       0x464F4343u
#define FLASH_FOC_CONFIG_VERSION     1u
#define FLASH_FOC_CONFIG_OFFSET      (USER_INFO_FLASH_PAGE_SIZE - sizeof(flash_foc_config_record_t))

/**
 * @brief FOC 配置扩展存储记录。
 */
typedef struct
{
    uint32_t magic;                     /**< FOC 配置扩展魔数，单位：无。 */
    uint32_t version;                   /**< FOC 配置扩展版本，单位：无。 */
    user_info_foc_config_t config;      /**< FOC 持久化配置。 */
    uint32_t crc32;                     /**< CRC32 校验值，单位：无。 */
} flash_foc_config_record_t;

int Flash_LoadFocConfig(user_info_foc_config_t *config);
int Flash_SaveFocConfig(const user_info_foc_config_t *config);

/**
 * @brief 计算 FOC 配置扩展记录 CRC32。
 * @param record FOC 配置扩展记录。
 * @return uint32_t CRC32 校验值，单位：无。
 */
static uint32_t flash_calc_foc_config_crc32(const flash_foc_config_record_t *record)
{
    flash_foc_config_record_t temp;

    if (record == NULL)
    {
        return 0u;
    }

    temp = *record;
    temp.crc32 = 0u;
    return UserInfo_CalcCrc32((const uint8_t *)&temp, (uint32_t)sizeof(temp));
}

/**
 * @brief 将通用用户信息转换为 App 参数记录。
 * @param info 通用用户信息。
 * @param data App 参数记录输出缓冲区。
 * @return void
 */
static void flash_copy_user_info_to_recoder(const user_info_data_t *info, recoder_data *data)
{
    user_info_foc_config_t foc_config;

    if ((info == NULL) || (data == NULL))
    {
        return;
    }

    data->calibration_angle = info->calibration_angle;
    data->can_id = info->can_id;
    data->ota = USER_INFO_OTA_FLAG_APP;
    data->can_configured = info->can_configured;
    data->report_enabled = info->report_enabled;
    data->report_period_ms = info->report_period_ms;

    if (Flash_LoadFocConfig(&foc_config) != USER_INFO_OK)
    {
        UserInfo_GetDefaultFocConfig(&foc_config);
    }

    data->pole_pairs = foc_config.pole_pairs;
    data->master_voltage_mv = foc_config.master_voltage_mv;
    data->control_hz = foc_config.control_hz;
    data->sensor_hz = foc_config.sensor_hz;
}

/**
 * @brief 将 App 参数记录转换为通用用户信息。
 * @param data App 参数记录。
 * @param info 通用用户信息输出缓冲区。
 * @return void
 */
static void flash_copy_recoder_to_user_info(recoder_data data, user_info_data_t *info)
{
    if (info == NULL)
    {
        return;
    }

    info->calibration_angle = data.calibration_angle;
    info->can_id = data.can_id;
    info->ota = USER_INFO_OTA_FLAG_APP;
    info->can_configured = data.can_configured;
    info->report_enabled = data.report_enabled;
    info->report_period_ms = data.report_period_ms;
}

/**
 * @brief 从 App 参数记录提取 FOC 配置。
 * @param data App 参数记录。
 * @param config FOC 配置输出缓冲区。
 * @return void
 */
static void flash_copy_recoder_to_foc_config(recoder_data data, user_info_foc_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->pole_pairs = data.pole_pairs;
    config->master_voltage_mv = data.master_voltage_mv;
    config->control_hz = data.control_hz;
    config->sensor_hz = data.sensor_hz;
}

/**
 * @brief 读取 App 参数记录。
 * @param data App 参数记录输出缓冲区。
 * @return uint8_t 成功返回 1，失败返回 0。
 */
uint8_t Load_Recoder(recoder_data *data)
{
    user_info_data_t info;

    if (data == NULL)
    {
        return 0u;
    }

    if (UserInfo_Load(&info) != USER_INFO_OK)
    {
        return 0u;
    }

    flash_copy_user_info_to_recoder(&info, data);
    return 1u;
}

/**
 * @brief 保存 App 参数记录。
 * @param data 待保存的 App 参数记录。
 * @return void
 */
void Save_Recoder(recoder_data data)
{
    user_info_data_t info;
    user_info_foc_config_t foc_config;

    flash_copy_recoder_to_user_info(data, &info);
    UserInfo_Save(&info);

    flash_copy_recoder_to_foc_config(data, &foc_config);
    if (UserInfo_ValidateFocConfig(&foc_config) == USER_INFO_OK)
    {
        Flash_SaveFocConfig(&foc_config);
    }
}

/**
 * @brief 读取 OTA 标志位。
 * @param ota_flag OTA 标志位输出缓冲区。
 * @return uint8_t 成功返回 1，失败返回 0。
 */
uint8_t Load_OtaFlag(uint32_t *ota_flag)
{
    if (OtaInfo_LoadFlag(ota_flag) != USER_INFO_OK)
    {
        return 0u;
    }

    return 1u;
}

/**
 * @brief 保存 OTA 标志位。
 * @param ota_flag OTA 标志位。
 * @return void
 */
void Save_OtaFlag(uint32_t ota_flag)
{
    OtaInfo_SaveFlag(ota_flag);
}

/**
 * @brief 读取 FOC 持久化配置。
 * @param config FOC 配置输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int Flash_LoadFocConfig(user_info_foc_config_t *config)
{
    const flash_foc_config_record_t *record;
    uint32_t crc32;

    if (config == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    record = (const flash_foc_config_record_t *)(USER_INFO_FLASH_ADDRESS + FLASH_FOC_CONFIG_OFFSET);
    if ((record->magic != FLASH_FOC_CONFIG_MAGIC) ||
        (record->version != FLASH_FOC_CONFIG_VERSION))
    {
        return UserInfo_GetDefaultFocConfig(config);
    }

    crc32 = flash_calc_foc_config_crc32(record);
    if (crc32 != record->crc32)
    {
        return UserInfo_GetDefaultFocConfig(config);
    }

    *config = record->config;
    UserInfo_NormalizeFocConfig(config);
    return USER_INFO_OK;
}

/**
 * @brief 保存 FOC 持久化配置。
 * @param config 待保存的 FOC 配置。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int Flash_SaveFocConfig(const user_info_foc_config_t *config)
{
    flash_foc_config_record_t record;

    if (UserInfo_ValidateFocConfig(config) != USER_INFO_OK)
    {
        return USER_INFO_ERR_PARAM;
    }

    memset(&record, 0, sizeof(record));
    record.magic = FLASH_FOC_CONFIG_MAGIC;
    record.version = FLASH_FOC_CONFIG_VERSION;
    record.config = *config;
    record.crc32 = flash_calc_foc_config_crc32(&record);

    return UserInfo_SavePageTail(FLASH_FOC_CONFIG_OFFSET,
                                 (const uint8_t *)&record,
                                 (uint32_t)sizeof(record));
}
