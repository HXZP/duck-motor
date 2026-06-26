#ifndef BOOT_LIGHT_H
#define BOOT_LIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Boot 灯效模式。
 */
typedef enum
{
    BOOT_LIGHT_MODE_STANDBY = 0u,       /**< 待机双闪模式。 */
    BOOT_LIGHT_MODE_UPGRADING           /**< 升级快闪模式。 */
} boot_light_mode_t;

/**
 * @brief 初始化 Boot 灯效模块。
 * @return void
 */
void BootLight_Init(void);

/**
 * @brief 设置 Boot 灯效模式。
 * @param mode 灯效模式。
 * @return void
 */
void BootLight_SetMode(boot_light_mode_t mode);

/**
 * @brief 轮询推进 Boot 灯效状态机。
 * @return void
 */
void BootLight_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_LIGHT_H */
