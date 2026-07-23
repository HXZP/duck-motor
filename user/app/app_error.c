#include "app/app_error.h"

static volatile uint32_t s_app_error_active_bits = APP_ERROR_NONE;
static volatile uint32_t s_app_error_latched_bits = APP_ERROR_NONE;

/**
 * @brief 初始化 App 错误记录。
 * @return void
 */
void AppError_Init(void)
{
    s_app_error_active_bits = APP_ERROR_NONE;
    s_app_error_latched_bits = APP_ERROR_NONE;
}

/**
 * @brief 设置一个或多个当前错误位并锁存历史错误。
 * @param error_bits 待设置的错误位集合。
 * @return void
 */
void AppError_Set(uint32_t error_bits)
{
    s_app_error_active_bits |= error_bits;
    s_app_error_latched_bits |= error_bits;
}

/**
 * @brief 清除一个或多个当前错误位。
 * @param error_bits 待清除的错误位集合。
 * @return void
 * @note 历史锁存错误不会被清除。
 */
void AppError_Clear(uint32_t error_bits)
{
    s_app_error_active_bits &= ~error_bits;
}

/**
 * @brief 获取当前仍然存在的错误位。
 * @return uint32_t 当前错误位集合。
 */
uint32_t AppError_GetActive(void)
{
    return s_app_error_active_bits;
}

/**
 * @brief 获取本次上电以来锁存的历史错误位。
 * @return uint32_t 历史错误位集合。
 */
uint32_t AppError_GetLatched(void)
{
    return s_app_error_latched_bits;
}

/**
 * @brief 判断当前是否存在指定错误。
 * @param error_mask 待检查的错误位掩码。
 * @return uint8_t 存在任一指定错误返回 1，否则返回 0。
 */
uint8_t AppError_HasActive(uint32_t error_mask)
{
    if ((s_app_error_active_bits & error_mask) != 0u)
    {
        return 1u;
    }

    return 0u;
}
