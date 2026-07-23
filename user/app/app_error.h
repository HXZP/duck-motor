#ifndef APP_ERROR_H
#define APP_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief App 错误位定义。
 */
typedef enum
{
    APP_ERROR_NONE = 0u,
    APP_ERROR_HARDWARE_I2C_INIT = (1u << 0),
    APP_ERROR_AS5600_STATUS_READ = (1u << 1),
    APP_ERROR_AS5600_CONFIG_READ = (1u << 2),
    APP_ERROR_AS5600_CONFIG_WRITE = (1u << 3),
    APP_ERROR_AS5600_CONFIG_VERIFY = (1u << 4),
    APP_ERROR_AS5600_MAGNET_TOO_STRONG = (1u << 5),
    APP_ERROR_AS5600_MAGNET_TOO_WEAK = (1u << 6),
    APP_ERROR_AS5600_MAGNET_NOT_DETECTED = (1u << 7),
    APP_ERROR_AS5600_MAGNET_STATUS_READ = (1u << 8),
    APP_ERROR_AS5600_READ_POINTER = (1u << 9),
    APP_ERROR_AS5600_RUNTIME_READ = (1u << 10),
    APP_ERROR_CAN_RECONFIGURE = (1u << 11),
    APP_ERROR_FOC_THREAD_CREATE = (1u << 12),
    APP_ERROR_CAN_THREAD_CREATE = (1u << 13)
} app_error_bit_t;

#define APP_ERROR_AS5600_INIT_MASK \
    (APP_ERROR_AS5600_STATUS_READ | \
     APP_ERROR_AS5600_CONFIG_READ | \
     APP_ERROR_AS5600_CONFIG_WRITE | \
     APP_ERROR_AS5600_CONFIG_VERIFY | \
     APP_ERROR_AS5600_MAGNET_TOO_STRONG | \
     APP_ERROR_AS5600_MAGNET_TOO_WEAK | \
     APP_ERROR_AS5600_MAGNET_NOT_DETECTED | \
     APP_ERROR_AS5600_MAGNET_STATUS_READ | \
     APP_ERROR_AS5600_READ_POINTER)

#define APP_ERROR_FOC_BLOCKING_MASK \
    (APP_ERROR_HARDWARE_I2C_INIT | \
     APP_ERROR_AS5600_STATUS_READ | \
     APP_ERROR_AS5600_CONFIG_READ | \
     APP_ERROR_AS5600_CONFIG_WRITE | \
     APP_ERROR_AS5600_CONFIG_VERIFY | \
     APP_ERROR_AS5600_MAGNET_NOT_DETECTED | \
     APP_ERROR_AS5600_MAGNET_STATUS_READ | \
     APP_ERROR_AS5600_READ_POINTER | \
     APP_ERROR_AS5600_RUNTIME_READ | \
     APP_ERROR_FOC_THREAD_CREATE)

/**
 * @brief 初始化 App 错误记录。
 * @return void
 */
void AppError_Init(void);

/**
 * @brief 设置一个或多个当前错误位并锁存历史错误。
 * @param error_bits 待设置的错误位集合。
 * @return void
 */
void AppError_Set(uint32_t error_bits);

/**
 * @brief 清除一个或多个当前错误位。
 * @param error_bits 待清除的错误位集合。
 * @return void
 * @note 历史锁存错误不会被清除。
 */
void AppError_Clear(uint32_t error_bits);

/**
 * @brief 获取当前仍然存在的错误位。
 * @return uint32_t 当前错误位集合。
 */
uint32_t AppError_GetActive(void);

/**
 * @brief 获取本次上电以来锁存的历史错误位。
 * @return uint32_t 历史错误位集合。
 */
uint32_t AppError_GetLatched(void);

/**
 * @brief 判断当前是否存在指定错误。
 * @param error_mask 待检查的错误位掩码。
 * @return uint8_t 存在任一指定错误返回 1，否则返回 0。
 */
uint8_t AppError_HasActive(uint32_t error_mask);

#ifdef __cplusplus
}
#endif

#endif /* APP_ERROR_H */
