#ifndef FOC_CONFIG_H
#define FOC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "common/user_info.h"
#include "foc/foc_core.h"

extern foc_cfg_t foc_runtime_config;

/**
 * @brief 从 Flash 加载 FOC 基础配置并应用到运行配置。
 * @return void
 */
void foc_config_load_saved(void);

/**
 * @brief 启动 FOC PWM 输出相关硬件。
 * @return void
 */
void foc_config_start_pwm(void);

/**
 * @brief 初始化功率使能 GPIO 并保持 FOC 输出关闭。
 * @return void
 */
void foc_config_prepare_safe_output(void);

/**
 * @brief 使能或关闭功率输出。
 * @param enable 输出使能标志，0 表示关闭，非 0 表示使能。
 * @return void
 */
void foc_output_enable(uint8_t enable);

/**
 * @brief 设置并应用 FOC 基础配置。
 * @param config FOC 基础配置。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int foc_config_set(const user_info_foc_config_t *config);

/**
 * @brief 获取当前 FOC 基础配置。
 * @param config FOC 基础配置输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int foc_config_get(user_info_foc_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* FOC_CONFIG_H */
