#include "common/user_info.h"

#include "stm32f1xx_hal.h"

#include <string.h>

static uint8_t s_user_info_page_buffer[USER_INFO_FLASH_PAGE_SIZE];

/**
 * @brief 初始化 CRC32 计算值。
 * @return uint32_t CRC32 初始值，单位：无。
 */
static uint32_t user_info_crc32_init(void)
{
    return 0xFFFFFFFFu;
}

/**
 * @brief 向 CRC32 中追加一段数据。
 * @param crc 当前 CRC32 中间值，单位：无。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return uint32_t 追加后的 CRC32 中间值，单位：无。
 */
static uint32_t user_info_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t index;

    if ((data == NULL) && (len != 0u))
    {
        return crc;
    }

    for (index = 0u; index < len; index++)
    {
        uint8_t bit;

        crc = crc ^ (uint32_t)data[index];
        for (bit = 0u; bit < 8u; bit++)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (crc >> 1u) ^ 0xEDB88320u;
            }
            else
            {
                crc = crc >> 1u;
            }
        }
    }

    return crc;
}

/**
 * @brief 结束 CRC32 计算。
 * @param crc 当前 CRC32 中间值，单位：无。
 * @return uint32_t CRC32 最终值，单位：无。
 */
static uint32_t user_info_crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

/**
 * @brief 计算 CRC32 校验值。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return uint32_t CRC32 校验值，单位：无。
 */
uint32_t UserInfo_CalcCrc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc;

    if ((data == NULL) && (len != 0u))
    {
        return 0u;
    }

    crc = user_info_crc32_init();
    crc = user_info_crc32_update(crc, data, len);
    return user_info_crc32_final(crc);
}

/**
 * @brief 计算用户信息记录 CRC32。
 * @param record 用户信息记录。
 * @return uint32_t CRC32 校验值，单位：无。
 */
static uint32_t user_info_calc_crc32(const user_info_record_t *record)
{
    uint32_t crc;

    if (record == NULL)
    {
        return 0u;
    }

    crc = user_info_crc32_init();
    crc = user_info_crc32_update(crc, (const uint8_t *)&record->magic, (uint32_t)sizeof(record->magic));
    crc = user_info_crc32_update(crc, (const uint8_t *)&record->version, (uint32_t)sizeof(record->version));
    crc = user_info_crc32_update(crc, (const uint8_t *)&record->data, (uint32_t)sizeof(record->data));
    return user_info_crc32_final(crc);
}

/**
 * @brief 判断节点 ID 是否允许作为已配置业务 ID。
 * @param node_id 待检查节点 ID，单位：无。
 * @return uint8_t 合法返回 1，否则返回 0。
 */
static uint8_t user_info_is_valid_configured_can_id(uint32_t node_id)
{
    if ((node_id < USER_INFO_CAN_ID_MIN) || (node_id > USER_INFO_CAN_ID_MAX))
    {
        return 0u;
    }

    if (node_id == USER_INFO_MANAGE_CAN_ID)
    {
        return 0u;
    }

    return 1u;
}

/**
 * @brief 归一化用户信息数据。
 * @param info 待归一化用户信息。
 * @return void
 */
static void user_info_normalize_data(user_info_data_t *info)
{
    if (info == NULL)
    {
        return;
    }

    if ((info->ota != USER_INFO_OTA_FLAG_APP) && (info->ota != USER_INFO_OTA_FLAG_BOOT))
    {
        info->ota = USER_INFO_OTA_FLAG_APP;
    }

    if (info->report_enabled != USER_INFO_REPORT_ENABLED)
    {
        info->report_enabled = USER_INFO_REPORT_DISABLED;
    }

    if ((info->report_period_ms < USER_INFO_MIN_REPORT_PERIOD_MS) ||
        (info->report_period_ms > USER_INFO_MAX_REPORT_PERIOD_MS))
    {
        info->report_period_ms = USER_INFO_DEFAULT_REPORT_PERIOD_MS;
    }

    if (info->can_configured == USER_INFO_CAN_CONFIGURED_YES)
    {
        if (user_info_is_valid_configured_can_id(info->can_id) == 0u)
        {
            info->can_id = USER_INFO_MANAGE_CAN_ID;
            info->can_configured = USER_INFO_CAN_CONFIGURED_NO;
        }
    }
    else
    {
        info->can_id = USER_INFO_MANAGE_CAN_ID;
        info->can_configured = USER_INFO_CAN_CONFIGURED_NO;
    }
}

/**
 * @brief 校验用户信息记录。
 * @param record 用户信息记录。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
static int user_info_validate_record(const user_info_record_t *record)
{
    uint32_t crc32;

    if (record == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    if ((record->magic != USER_INFO_MAGIC) || (record->version != USER_INFO_VERSION))
    {
        return USER_INFO_ERR_VERIFY;
    }

    crc32 = user_info_calc_crc32(record);
    if (crc32 != record->crc32)
    {
        return USER_INFO_ERR_VERIFY;
    }

    return USER_INFO_OK;
}

/**
 * @brief 将用户信息打包成 Flash 记录。
 * @param info 用户信息业务数据。
 * @param record 用户信息记录输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
static int user_info_pack_record(const user_info_data_t *info, user_info_record_t *record)
{
    if ((info == NULL) || (record == NULL))
    {
        return USER_INFO_ERR_PARAM;
    }

    memset(record, 0, sizeof(*record));
    record->magic = USER_INFO_MAGIC;
    record->version = USER_INFO_VERSION;
    record->data = *info;
    user_info_normalize_data(&record->data);
    record->crc32 = user_info_calc_crc32(record);

    return USER_INFO_OK;
}

/**
 * @brief 擦除用户信息 Flash 页。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR。
 */
static int user_info_erase_page(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0u;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = USER_INFO_FLASH_ADDRESS;
    erase.NbPages = 1u;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        return USER_INFO_ERR;
    }

    return USER_INFO_OK;
}

/**
 * @brief 获取默认用户信息。
 * @param info 用户信息输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_GetDefault(user_info_data_t *info)
{
    if (info == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    memset(info, 0, sizeof(*info));
    info->calibration_angle = 0;
    info->can_id = USER_INFO_MANAGE_CAN_ID;
    info->ota = USER_INFO_OTA_FLAG_APP;
    info->can_configured = USER_INFO_CAN_CONFIGURED_NO;
    info->report_enabled = USER_INFO_DEFAULT_REPORT_ENABLE;
    info->report_period_ms = USER_INFO_DEFAULT_REPORT_PERIOD_MS;

    return USER_INFO_OK;
}

/**
 * @brief 获取默认 FOC 配置。
 * @param config FOC 配置输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_GetDefaultFocConfig(user_info_foc_config_t *config)
{
    if (config == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    config->pole_pairs = USER_INFO_DEFAULT_POLE_PAIRS;
    config->master_voltage_mv = USER_INFO_DEFAULT_MASTER_VOLTAGE_MV;
    config->control_hz = USER_INFO_DEFAULT_CONTROL_HZ;
    config->sensor_hz = USER_INFO_DEFAULT_SENSOR_HZ;
    config->phase_map = USER_INFO_DEFAULT_PHASE_MAP;
    return USER_INFO_OK;
}

/**
 * @brief 校验 FOC 配置是否在数值合法范围内。
 * @param config 待校验的 FOC 配置。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_ValidateFocConfig(const user_info_foc_config_t *config)
{
    if (config == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    if ((config->pole_pairs < USER_INFO_MIN_POLE_PAIRS) ||
        (config->pole_pairs > USER_INFO_MAX_POLE_PAIRS))
    {
        return USER_INFO_ERR_PARAM;
    }

    if ((config->master_voltage_mv < USER_INFO_MIN_MASTER_VOLTAGE_MV) ||
        (config->master_voltage_mv > USER_INFO_MAX_MASTER_VOLTAGE_MV))
    {
        return USER_INFO_ERR_PARAM;
    }

    if ((config->control_hz < USER_INFO_MIN_LOOP_HZ) ||
        (config->control_hz > USER_INFO_MAX_LOOP_HZ))
    {
        return USER_INFO_ERR_PARAM;
    }

    if ((config->sensor_hz < USER_INFO_MIN_LOOP_HZ) ||
        (config->sensor_hz > USER_INFO_MAX_LOOP_HZ))
    {
        return USER_INFO_ERR_PARAM;
    }

    if ((config->phase_map < USER_INFO_MIN_PHASE_MAP) ||
        (config->phase_map > USER_INFO_MAX_PHASE_MAP))
    {
        return USER_INFO_ERR_PARAM;
    }

    return USER_INFO_OK;
}

/**
 * @brief 归一化 FOC 配置。
 * @param config 待归一化的 FOC 配置。
 * @return void
 */
void UserInfo_NormalizeFocConfig(user_info_foc_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    if ((config->pole_pairs < USER_INFO_MIN_POLE_PAIRS) ||
        (config->pole_pairs > USER_INFO_MAX_POLE_PAIRS))
    {
        config->pole_pairs = USER_INFO_DEFAULT_POLE_PAIRS;
    }

    if ((config->master_voltage_mv < USER_INFO_MIN_MASTER_VOLTAGE_MV) ||
        (config->master_voltage_mv > USER_INFO_MAX_MASTER_VOLTAGE_MV))
    {
        config->master_voltage_mv = USER_INFO_DEFAULT_MASTER_VOLTAGE_MV;
    }

    if ((config->control_hz < USER_INFO_MIN_LOOP_HZ) ||
        (config->control_hz > USER_INFO_MAX_LOOP_HZ))
    {
        config->control_hz = USER_INFO_DEFAULT_CONTROL_HZ;
    }

    if ((config->sensor_hz < USER_INFO_MIN_LOOP_HZ) ||
        (config->sensor_hz > USER_INFO_MAX_LOOP_HZ))
    {
        config->sensor_hz = USER_INFO_DEFAULT_SENSOR_HZ;
    }

    if ((config->phase_map < USER_INFO_MIN_PHASE_MAP) ||
        (config->phase_map > USER_INFO_MAX_PHASE_MAP))
    {
        config->phase_map = USER_INFO_DEFAULT_PHASE_MAP;
    }
}

/**
 * @brief 读取用户信息。
 * @param info 用户信息输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_Load(user_info_data_t *info)
{
    const user_info_record_t *record;

    if (info == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    record = (const user_info_record_t *)USER_INFO_FLASH_ADDRESS;
    if (user_info_validate_record(record) != USER_INFO_OK)
    {
        return UserInfo_GetDefault(info);
    }

    *info = record->data;
    user_info_normalize_data(info);

    return USER_INFO_OK;
}

/**
 * @brief 保存用户信息。
 * @param info 待保存的用户信息。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_Save(const user_info_data_t *info)
{
    HAL_StatusTypeDef status;
    user_info_record_t record;
    uint32_t offset;
    int ret;

    if (info == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    ret = user_info_pack_record(info, &record);
    if (ret != USER_INFO_OK)
    {
        return ret;
    }

    memcpy(s_user_info_page_buffer,
           (const uint8_t *)USER_INFO_FLASH_ADDRESS,
           USER_INFO_FLASH_PAGE_SIZE);
    memcpy(s_user_info_page_buffer, &record, sizeof(record));

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        return USER_INFO_ERR;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);

    ret = user_info_erase_page();
    if (ret == USER_INFO_OK)
    {
        for (offset = 0u; offset < USER_INFO_FLASH_PAGE_SIZE; offset += 4u)
        {
            uint32_t word;

            memcpy(&word, &s_user_info_page_buffer[offset], sizeof(word));
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, USER_INFO_FLASH_ADDRESS + offset, word) != HAL_OK)
            {
                ret = USER_INFO_ERR;
                break;
            }
        }
    }

    HAL_FLASH_Lock();
    return ret;
}

/**
 * @brief 更新用户信息页尾部扩展数据。
 * @param page_offset 页内偏移，单位：字节。
 * @param data 扩展数据缓冲区。
 * @param len 扩展数据长度，单位：字节。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_SavePageTail(uint32_t page_offset, const uint8_t *data, uint32_t len)
{
    uint32_t offset;

    if ((data == NULL) && (len != 0u))
    {
        return USER_INFO_ERR_PARAM;
    }

    if ((page_offset > USER_INFO_FLASH_PAGE_SIZE) ||
        (len > (USER_INFO_FLASH_PAGE_SIZE - page_offset)))
    {
        return USER_INFO_ERR_PARAM;
    }

    memcpy(s_user_info_page_buffer,
           (const uint8_t *)USER_INFO_FLASH_ADDRESS,
           USER_INFO_FLASH_PAGE_SIZE);

    if (len != 0u)
    {
        memcpy(&s_user_info_page_buffer[page_offset], data, len);
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return USER_INFO_ERR;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);

    if (user_info_erase_page() != USER_INFO_OK)
    {
        HAL_FLASH_Lock();
        return USER_INFO_ERR;
    }

    for (offset = 0u; offset < USER_INFO_FLASH_PAGE_SIZE; offset += 4u)
    {
        uint32_t word;

        memcpy(&word, &s_user_info_page_buffer[offset], sizeof(word));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, USER_INFO_FLASH_ADDRESS + offset, word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return USER_INFO_ERR;
        }
    }

    HAL_FLASH_Lock();
    return USER_INFO_OK;
}

/**
 * @brief 读取 OTA 标志位。
 * @param ota_flag OTA 标志位输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_LoadOtaFlag(uint32_t *ota_flag)
{
    user_info_data_t info;
    int ret;

    if (ota_flag == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    ret = UserInfo_Load(&info);
    if (ret != USER_INFO_OK)
    {
        return ret;
    }

    *ota_flag = info.ota;
    return USER_INFO_OK;
}

/**
 * @brief 保存 OTA 标志位。
 * @param ota_flag OTA 标志位。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int UserInfo_SaveOtaFlag(uint32_t ota_flag)
{
    user_info_data_t info;
    int ret;

    ret = UserInfo_Load(&info);
    if (ret != USER_INFO_OK)
    {
        return ret;
    }

    info.ota = ota_flag;
    return UserInfo_Save(&info);
}
