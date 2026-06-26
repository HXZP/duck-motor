#ifndef APP_LIGHT_H
#define APP_LIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief App 灯效模式。
 */
typedef enum
{
    APP_LIGHT_MODE_UNCONFIGURED = 0, /**< 未配置低待机灯效。 */
    APP_LIGHT_MODE_RUNNING = 1,      /**< 已配置运行灯效。 */
    APP_LIGHT_MODE_IDENTIFY = 2      /**< 上位机识别灯效。 */
} app_light_mode_t;

/**
 * @brief 初始化 App 灯效模块。
 * @return void
 */
void AppLight_Init(void);

/**
 * @brief 设置 App 基础灯效模式。
 * @param mode 目标灯效模式。
 * @return void
 */
void AppLight_SetMode(app_light_mode_t mode);

/**
 * @brief 启动 App 识别灯效。
 * @param frequency_hz 闪烁频率，单位：Hz。
 * @param duration_ms 持续时间，单位：毫秒。
 * @return void
 */
void AppLight_StartIdentify(uint8_t frequency_hz, uint16_t duration_ms);

/**
 * @brief 轮询推进 App 灯效状态机。
 * @return void
 */
void AppLight_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LIGHT_H */
