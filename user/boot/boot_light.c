#include "boot/boot_light.h"

#include "main.h"
#include "stm32f1xx_hal.h"

#define BOOT_LIGHT_STANDBY_PERIOD_MS              1400u   /**< 待机灯效周期，单位：毫秒。 */
#define BOOT_LIGHT_STANDBY_FIRST_ON_END_MS        80u     /**< 待机第一段亮灯结束时间，单位：毫秒。 */
#define BOOT_LIGHT_STANDBY_SECOND_ON_START_MS     180u    /**< 待机第二段亮灯开始时间，单位：毫秒。 */
#define BOOT_LIGHT_STANDBY_SECOND_ON_END_MS       260u    /**< 待机第二段亮灯结束时间，单位：毫秒。 */
#define BOOT_LIGHT_UPGRADING_ON_TIME_MS           100u    /**< 升级亮灯时间，单位：毫秒。 */
#define BOOT_LIGHT_UPGRADING_PERIOD_MS            200u    /**< 升级灯效周期，单位：毫秒。 */

/**
 * @brief Boot 灯效运行上下文。
 */
typedef struct
{
    boot_light_mode_t mode;             /**< 当前灯效模式。 */
    uint32_t mode_start_tick_ms;        /**< 模式启动时间，单位：毫秒。 */
} boot_light_context_t;

static boot_light_context_t s_boot_light_ctx = {0};

/**
 * @brief 写入 LED 引脚电平。
 * @param enabled 是否点亮 LED，非 0 表示点亮，0 表示熄灭。
 * @return void
 */
static void boot_light_write_led(uint8_t enabled)
{
    if (enabled != 0u)
    {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    }
}

/**
 * @brief 计算 Boot 待机灯效是否点亮。
 * @param elapsed_ms 当前模式已运行时间，单位：毫秒。
 * @return uint8_t 需要点亮返回 1，否则返回 0。
 */
static uint8_t boot_light_calc_standby_enabled(uint32_t elapsed_ms)
{
    uint32_t phase_ms = elapsed_ms % BOOT_LIGHT_STANDBY_PERIOD_MS;

    if (phase_ms < BOOT_LIGHT_STANDBY_FIRST_ON_END_MS)
    {
        return 1u;
    }

    if ((phase_ms >= BOOT_LIGHT_STANDBY_SECOND_ON_START_MS) &&
        (phase_ms < BOOT_LIGHT_STANDBY_SECOND_ON_END_MS))
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief 计算当前模式中 LED 是否应该点亮。
 * @param mode 灯效模式。
 * @param elapsed_ms 当前模式已运行时间，单位：毫秒。
 * @return uint8_t 需要点亮返回 1，否则返回 0。
 */
static uint8_t boot_light_calc_enabled(boot_light_mode_t mode, uint32_t elapsed_ms)
{
    uint32_t phase_ms;

    if (mode == BOOT_LIGHT_MODE_UPGRADING)
    {
        phase_ms = elapsed_ms % BOOT_LIGHT_UPGRADING_PERIOD_MS;
        if (phase_ms < BOOT_LIGHT_UPGRADING_ON_TIME_MS)
        {
            return 1u;
        }

        return 0u;
    }

    return boot_light_calc_standby_enabled(elapsed_ms);
}

/**
 * @brief 初始化 Boot 灯效模块。
 * @return void
 */
void BootLight_Init(void)
{
    s_boot_light_ctx.mode = BOOT_LIGHT_MODE_STANDBY;
    s_boot_light_ctx.mode_start_tick_ms = HAL_GetTick();
    boot_light_write_led(0u);
}

/**
 * @brief 设置 Boot 灯效模式。
 * @param mode 灯效模式。
 * @return void
 */
void BootLight_SetMode(boot_light_mode_t mode)
{
    if (s_boot_light_ctx.mode == mode)
    {
        return;
    }

    s_boot_light_ctx.mode = mode;
    s_boot_light_ctx.mode_start_tick_ms = HAL_GetTick();
}

/**
 * @brief 轮询推进 Boot 灯效状态机。
 * @return void
 */
void BootLight_Poll(void)
{
    uint32_t now_ms;
    uint32_t elapsed_ms;
    uint8_t led_enabled;

    now_ms = HAL_GetTick();
    elapsed_ms = now_ms - s_boot_light_ctx.mode_start_tick_ms;
    led_enabled = boot_light_calc_enabled(s_boot_light_ctx.mode, elapsed_ms);
    boot_light_write_led(led_enabled);
}
