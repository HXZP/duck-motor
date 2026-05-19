#include "common/user_info.h"

#include "stm32f1xx_hal.h"

#include <string.h>

#define USER_INFO_LEGACY_MAGIC  0xA5A5A5A5u

/**
 * @brief 旧版 App 参数记录。
 */
typedef struct
{
    int32_t calibration_angle;      /**< 校准角度，单位：内部角度计数。 */
    uint32_t can_id;                /**< CAN 节点 ID，单位：无。 */
} user_info_legacy_data_t;

/**
 * @brief 旧版用户信息 Flash 存储记录。
 */
typedef struct
{
    uint32_t magic;                 /**< 旧版记录魔数，单位：无。 */
    user_info_legacy_data_t data;   /**< 旧版用户信息业务数据。 */
    uint32_t crc32;                 /**< 旧版 CRC32 校验值，单位：无。 */
} user_info_legacy_record_t;

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
 * @brief 尝试读取旧版用户信息记录。
 * @param info 用户信息输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
static int user_info_try_load_legacy(user_info_data_t *info)
{
    const user_info_legacy_record_t *legacy_record;

    if (info == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    legacy_record = (const user_info_legacy_record_t *)USER_INFO_FLASH_ADDRESS;
    if (legacy_record->magic != USER_INFO_LEGACY_MAGIC)
    {
        return USER_INFO_ERR_VERIFY;
    }

    if ((legacy_record->data.can_id == 0u) || (legacy_record->data.can_id > 0x7Fu))
    {
        return USER_INFO_ERR_VERIFY;
    }

    info->calibration_angle = legacy_record->data.calibration_angle;
    info->can_id = legacy_record->data.can_id;
    info->ota = USER_INFO_OTA_FLAG_APP;
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
 * @brief 将记录写入用户信息 Flash 页。
 * @param record 用户信息记录。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR。
 */
static int user_info_program_record(const user_info_record_t *record)
{
    uint32_t offset;
    const uint8_t *data;

    if (record == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    data = (const uint8_t *)record;
    for (offset = 0u; offset < (uint32_t)sizeof(*record); offset += 4u)
    {
        uint32_t word = 0xFFFFFFFFu;
        uint32_t chunk = (uint32_t)sizeof(*record) - offset;

        if (chunk > 4u)
        {
            chunk = 4u;
        }

        memcpy(&word, &data[offset], chunk);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, USER_INFO_FLASH_ADDRESS + offset, word) != HAL_OK)
        {
            return USER_INFO_ERR;
        }
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
    info->can_id = USER_INFO_DEFAULT_CAN_ID;
    info->ota = USER_INFO_OTA_FLAG_APP;

    return USER_INFO_OK;
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
        if (user_info_try_load_legacy(info) == USER_INFO_OK)
        {
            return USER_INFO_OK;
        }

        return UserInfo_GetDefault(info);
    }

    *info = record->data;
    if ((info->can_id == 0u) || (info->can_id > 0x7Fu))
    {
        info->can_id = USER_INFO_DEFAULT_CAN_ID;
    }

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

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        return USER_INFO_ERR;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);

    ret = user_info_erase_page();
    if (ret == USER_INFO_OK)
    {
        ret = user_info_program_record(&record);
    }

    HAL_FLASH_Lock();
    return ret;
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
