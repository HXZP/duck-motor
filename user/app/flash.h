#ifndef __FLASH_H__
#define __FLASH_H__

#include <stdint.h>

#include "common/user_info.h"
#include "stm32f1xx_hal.h"

#define RECODER_DEFAULT_CALIBRATION_ANGLE (0)
#define RECODER_DEFAULT_CAN_ID            (USER_INFO_DEFAULT_CAN_ID)

/**
 * @brief App 参数记录。
 */
typedef struct
{
    int32_t calibration_angle; /**< 校准角度，单位：内部角度计数。 */
    uint32_t can_id;           /**< CAN 节点 ID，单位：无。 */
    uint32_t ota;              /**< OTA 标志位，0 表示允许跳转 App，非 0 表示停留 Boot。 */
} recoder_data;

/**
 * @brief 兼容旧代码的用户信息记录结构。
 */
typedef struct
{
    uint32_t magic;            /**< 数据头标志，单位：无。 */
    uint32_t version;          /**< 数据格式版本，单位：无。 */
    recoder_data data;         /**< App 参数记录。 */
    uint32_t crc32;            /**< 校验码，单位：无。 */
} MotorCalibrationData;

/**
 * @brief 读取 App 参数记录。
 * @param data App 参数记录输出缓冲区。
 * @return uint8_t 成功返回 1，失败返回 0。
 */
uint8_t Load_Recoder(recoder_data *data);

/**
 * @brief 保存 App 参数记录。
 * @param data 待保存的 App 参数记录。
 * @return void
 */
void Save_Recoder(recoder_data data);

/**
 * @brief 读取 OTA 标志位。
 * @param ota_flag OTA 标志位输出缓冲区。
 * @return uint8_t 成功返回 1，失败返回 0。
 */
uint8_t Load_OtaFlag(uint32_t *ota_flag);

/**
 * @brief 保存 OTA 标志位。
 * @param ota_flag OTA 标志位。
 * @return void
 */
void Save_OtaFlag(uint32_t ota_flag);

#endif
