#include "app/flash.h"

/**
 * @brief 将通用用户信息转换为 App 参数记录。
 * @param info 通用用户信息。
 * @param data App 参数记录输出缓冲区。
 * @return void
 */
static void flash_copy_user_info_to_recoder(const user_info_data_t *info, recoder_data *data)
{
    if ((info == NULL) || (data == NULL))
    {
        return;
    }

    data->calibration_angle = info->calibration_angle;
    data->can_id = info->can_id;
    data->ota = info->ota;
    data->can_configured = info->can_configured;
    data->report_enabled = info->report_enabled;
    data->report_period_ms = info->report_period_ms;
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
    info->ota = data.ota;
    info->can_configured = data.can_configured;
    info->report_enabled = data.report_enabled;
    info->report_period_ms = data.report_period_ms;
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

    flash_copy_recoder_to_user_info(data, &info);
    UserInfo_Save(&info);
}

/**
 * @brief 读取 OTA 标志位。
 * @param ota_flag OTA 标志位输出缓冲区。
 * @return uint8_t 成功返回 1，失败返回 0。
 */
uint8_t Load_OtaFlag(uint32_t *ota_flag)
{
    if (UserInfo_LoadOtaFlag(ota_flag) != USER_INFO_OK)
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
    UserInfo_SaveOtaFlag(ota_flag);
}
