#include "common/ota_info.h"

#include "stm32f1xx_hal.h"

#include <string.h>

/**
 * @brief 归一化 OTA 信息数据。
 * @param info 待归一化的 OTA 信息。
 * @return void
 */
static void ota_info_normalize_data(ota_info_data_t *info)
{
    if (info == NULL)
    {
        return;
    }

    if ((info->ota_flag != OTA_INFO_FLAG_APP) && (info->ota_flag != OTA_INFO_FLAG_BOOT))
    {
        info->ota_flag = OTA_INFO_FLAG_BOOT;
    }

    if (info->app_size > FLASH_LAYOUT_APP_SIZE)
    {
        info->app_size = 0u;
        info->app_crc16 = 0u;
    }
}

/**
 * @brief 计算 OTA 信息记录 CRC32。
 * @param record OTA 信息记录。
 * @return uint32_t CRC32 校验值，单位：无。
 */
static uint32_t ota_info_calc_crc32(const ota_info_record_t *record)
{
    ota_info_record_t temp;

    if (record == NULL)
    {
        return 0u;
    }

    temp = *record;
    temp.crc32 = 0u;
    return UserInfo_CalcCrc32((const uint8_t *)&temp, (uint32_t)sizeof(temp));
}

/**
 * @brief 校验 OTA 信息记录。
 * @param record OTA 信息记录。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
static int ota_info_validate_record(const ota_info_record_t *record)
{
    uint32_t crc32;

    if (record == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    if ((record->magic != OTA_INFO_MAGIC) || (record->version != OTA_INFO_VERSION))
    {
        return USER_INFO_ERR_VERIFY;
    }

    crc32 = ota_info_calc_crc32(record);
    if (crc32 != record->crc32)
    {
        return USER_INFO_ERR_VERIFY;
    }

    return USER_INFO_OK;
}

/**
 * @brief 将 OTA 信息打包成 Flash 记录。
 * @param info OTA 信息业务数据。
 * @param record OTA 信息记录输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
static int ota_info_pack_record(const ota_info_data_t *info, ota_info_record_t *record)
{
    if ((info == NULL) || (record == NULL))
    {
        return USER_INFO_ERR_PARAM;
    }

    memset(record, 0, sizeof(*record));
    record->magic = OTA_INFO_MAGIC;
    record->version = OTA_INFO_VERSION;
    record->data = *info;
    ota_info_normalize_data(&record->data);
    record->crc32 = ota_info_calc_crc32(record);

    return USER_INFO_OK;
}

/**
 * @brief 擦除 OTA 信息 Flash 页。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR。
 */
static int ota_info_erase_page(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0u;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = OTA_INFO_FLASH_ADDRESS;
    erase.NbPages = 1u;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        return USER_INFO_ERR;
    }

    return USER_INFO_OK;
}

/**
 * @brief 获取默认 OTA 信息。
 * @param info OTA 信息输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_GetDefault(ota_info_data_t *info)
{
    if (info == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    memset(info, 0, sizeof(*info));
    info->ota_flag = OTA_INFO_FLAG_BOOT;
    info->app_size = 0u;
    info->app_crc16 = 0u;
    info->reserved = 0u;
    return USER_INFO_OK;
}

/**
 * @brief 读取 OTA 信息。
 * @param info OTA 信息输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_Load(ota_info_data_t *info)
{
    const ota_info_record_t *record;

    if (info == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    record = (const ota_info_record_t *)OTA_INFO_FLASH_ADDRESS;
    if (ota_info_validate_record(record) != USER_INFO_OK)
    {
        return OtaInfo_GetDefault(info);
    }

    *info = record->data;
    ota_info_normalize_data(info);

    return USER_INFO_OK;
}

/**
 * @brief 保存 OTA 信息。
 * @param info 待保存的 OTA 信息。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_Save(const ota_info_data_t *info)
{
    HAL_StatusTypeDef status;
    ota_info_record_t record;
    uint32_t offset;
    int ret;

    ret = ota_info_pack_record(info, &record);
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

    ret = ota_info_erase_page();
    if (ret == USER_INFO_OK)
    {
        for (offset = 0u; offset < sizeof(record); offset += 4u)
        {
            uint32_t word;

            memcpy(&word, ((const uint8_t *)&record) + offset, sizeof(word));
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, OTA_INFO_FLASH_ADDRESS + offset, word) != HAL_OK)
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
 * @brief 读取 OTA 标志位。
 * @param ota_flag OTA 标志位输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_LoadFlag(uint32_t *ota_flag)
{
    ota_info_data_t info;
    int ret;

    if (ota_flag == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    ret = OtaInfo_Load(&info);
    if (ret != USER_INFO_OK)
    {
        return ret;
    }

    *ota_flag = info.ota_flag;
    return USER_INFO_OK;
}

/**
 * @brief 保存 OTA 标志位。
 * @param ota_flag OTA 标志位。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_SaveFlag(uint32_t ota_flag)
{
    ota_info_data_t info;
    int ret;

    ret = OtaInfo_Load(&info);
    if (ret != USER_INFO_OK)
    {
        return ret;
    }

    info.ota_flag = ota_flag;
    return OtaInfo_Save(&info);
}

/**
 * @brief 保存 App 校验信息并允许跳转 App。
 * @param app_size App 镜像长度，单位：字节。
 * @param app_crc16 App 镜像 CRC16-CCITT 校验值，单位：无。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int OtaInfo_SaveAppValid(uint32_t app_size, uint16_t app_crc16)
{
    ota_info_data_t info;

    if ((app_size == 0u) || (app_size > FLASH_LAYOUT_APP_SIZE))
    {
        return USER_INFO_ERR_PARAM;
    }

    info.ota_flag = OTA_INFO_FLAG_APP;
    info.app_size = app_size;
    info.app_crc16 = app_crc16;
    info.reserved = 0u;

    return OtaInfo_Save(&info);
}
