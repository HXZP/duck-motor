#include "app/app_light.h"

#include "main.h"
#include "stm32f1xx_hal.h"

#define APP_LIGHT_UNCONFIGURED_ON_TIME_MS       50u     /**< 未配置亮灯时间，单位：毫秒。 */
#define APP_LIGHT_UNCONFIGURED_PERIOD_MS        2000u   /**< 未配置灯效周期，单位：毫秒。 */
#define APP_LIGHT_RUNNING_PERIOD_MS             1500u   /**< 运行灯效周期，单位：毫秒。 */
#define APP_LIGHT_RUNNING_FIRST_ON_END_MS       100u    /**< 运行第一段亮灯结束时间，单位：毫秒。 */
#define APP_LIGHT_RUNNING_SECOND_ON_START_MS    200u    /**< 运行第二段亮灯开始时间，单位：毫秒。 */
#define APP_LIGHT_RUNNING_SECOND_ON_END_MS      300u    /**< 运行第二段亮灯结束时间，单位：毫秒。 */
#define APP_LIGHT_RUNNING_THIRD_ON_START_MS     400u    /**< 运行第三段亮灯开始时间，单位：毫秒。 */
#define APP_LIGHT_RUNNING_THIRD_ON_END_MS       500u    /**< 运行第三段亮灯结束时间，单位：毫秒。 */
#define APP_LIGHT_IDENTIFY_DEFAULT_DURATION_MS  5000u   /**< 识别灯效默认持续时间，单位：毫秒。 */
#define APP_LIGHT_IDENTIFY_MIN_FREQUENCY_HZ     1u      /**< 识别灯效最低频率，单位：Hz。 */
#define APP_LIGHT_IDENTIFY_MAX_FREQUENCY_HZ     20u     /**< 识别灯效最高频率，单位：Hz。 */
#define APP_LIGHT_IDENTIFY_MIN_HALF_PERIOD_MS   25u     /**< 识别灯效最短半周期，单位：毫秒。 */

/**
 * @brief App 灯效运行上下文。
 */
typedef struct
{
    app_light_mode_t mode;              /**< 当前灯效模式。 */
    app_light_mode_t base_mode;         /**< 识别结束后恢复的基础模式。 */
    uint32_t mode_start_tick_ms;        /**< 模式启动时间，单位：毫秒。 */
    uint32_t identify_duration_ms;      /**< 识别持续时间，单位：毫秒。 */
    uint32_t identify_half_period_ms;   /**< 识别闪烁半周期，单位：毫秒。 */
} app_light_context_t;

static app_light_context_t s_app_light_ctx = {0};

/**
 * @brief 写入 LED 引脚电平。
 * @param enabled 是否点亮 LED，非 0 表示点亮，0 表示熄灭。
 * @return void
 */
static void app_light_write_led(uint8_t enabled)
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
 * @brief 计算运行态灯效是否点亮。
 * @param elapsed_ms 当前模式已运行时间，单位：毫秒。
 * @return uint8_t 需要点亮返回 1，否则返回 0。
 */
static uint8_t app_light_calc_running_enabled(uint32_t elapsed_ms)
{
    uint32_t phase_ms = elapsed_ms % APP_LIGHT_RUNNING_PERIOD_MS;

    if (phase_ms < APP_LIGHT_RUNNING_FIRST_ON_END_MS)
    {
        return 1u;
    }

    if ((phase_ms >= APP_LIGHT_RUNNING_SECOND_ON_START_MS) &&
        (phase_ms < APP_LIGHT_RUNNING_SECOND_ON_END_MS))
    {
        return 1u;
    }

    if ((phase_ms >= APP_LIGHT_RUNNING_THIRD_ON_START_MS) &&
        (phase_ms < APP_LIGHT_RUNNING_THIRD_ON_END_MS))
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief 计算基础灯效是否点亮。
 * @param mode 当前基础灯效模式。
 * @param elapsed_ms 当前模式已运行时间，单位：毫秒。
 * @return uint8_t 需要点亮返回 1，否则返回 0。
 */
static uint8_t app_light_calc_base_enabled(app_light_mode_t mode, uint32_t elapsed_ms)
{
    uint32_t phase_ms;

    if (mode == APP_LIGHT_MODE_RUNNING)
    {
        return app_light_calc_running_enabled(elapsed_ms);
    }

    phase_ms = elapsed_ms % APP_LIGHT_UNCONFIGURED_PERIOD_MS;
    if (phase_ms < APP_LIGHT_UNCONFIGURED_ON_TIME_MS)
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief 计算识别灯效是否点亮。
 * @param elapsed_ms 识别灯效已运行时间，单位：毫秒。
 * @return uint8_t 需要点亮返回 1，否则返回 0。
 */
static uint8_t app_light_calc_identify_enabled(uint32_t elapsed_ms)
{
    uint32_t phase_ms;

    phase_ms = elapsed_ms % (s_app_light_ctx.identify_half_period_ms * 2u);
    if (phase_ms < s_app_light_ctx.identify_half_period_ms)
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief 初始化 App 灯效模块。
 * @return void
 */
void AppLight_Init(void)
{
    s_app_light_ctx.mode = APP_LIGHT_MODE_UNCONFIGURED;
    s_app_light_ctx.base_mode = APP_LIGHT_MODE_UNCONFIGURED;
    s_app_light_ctx.mode_start_tick_ms = HAL_GetTick();
    s_app_light_ctx.identify_duration_ms = APP_LIGHT_IDENTIFY_DEFAULT_DURATION_MS;
    s_app_light_ctx.identify_half_period_ms = 250u;
    app_light_write_led(0u);
}

/**
 * @brief 设置 App 基础灯效模式。
 * @param mode 目标灯效模式。
 * @return void
 */
void AppLight_SetMode(app_light_mode_t mode)
{
    if (mode == APP_LIGHT_MODE_IDENTIFY)
    {
        return;
    }

    s_app_light_ctx.base_mode = mode;
    if (s_app_light_ctx.mode == APP_LIGHT_MODE_IDENTIFY)
    {
        return;
    }

    if (s_app_light_ctx.mode == mode)
    {
        return;
    }

    s_app_light_ctx.mode = mode;
    s_app_light_ctx.mode_start_tick_ms = HAL_GetTick();
}

/**
 * @brief 启动 App 识别灯效。
 * @param frequency_hz 闪烁频率，单位：Hz。
 * @param duration_ms 持续时间，单位：毫秒。
 * @return void
 */
void AppLight_StartIdentify(uint8_t frequency_hz, uint16_t duration_ms)
{
    uint32_t half_period_ms;

    if (frequency_hz < APP_LIGHT_IDENTIFY_MIN_FREQUENCY_HZ)
    {
        frequency_hz = APP_LIGHT_IDENTIFY_MIN_FREQUENCY_HZ;
    }

    if (frequency_hz > APP_LIGHT_IDENTIFY_MAX_FREQUENCY_HZ)
    {
        frequency_hz = APP_LIGHT_IDENTIFY_MAX_FREQUENCY_HZ;
    }

    if (duration_ms == 0u)
    {
        duration_ms = APP_LIGHT_IDENTIFY_DEFAULT_DURATION_MS;
    }

    half_period_ms = 500u / (uint32_t)frequency_hz;
    if (half_period_ms < APP_LIGHT_IDENTIFY_MIN_HALF_PERIOD_MS)
    {
        half_period_ms = APP_LIGHT_IDENTIFY_MIN_HALF_PERIOD_MS;
    }

    if (s_app_light_ctx.mode != APP_LIGHT_MODE_IDENTIFY)
    {
        s_app_light_ctx.base_mode = s_app_light_ctx.mode;
    }

    s_app_light_ctx.mode = APP_LIGHT_MODE_IDENTIFY;
    s_app_light_ctx.mode_start_tick_ms = HAL_GetTick();
    s_app_light_ctx.identify_duration_ms = duration_ms;
    s_app_light_ctx.identify_half_period_ms = half_period_ms;
}

/**
 * @brief 轮询推进 App 灯效状态机。
 * @return void
 */
void AppLight_Poll(void)
{
    uint32_t now_ms;
    uint32_t elapsed_ms;
    uint8_t led_enabled;

    now_ms = HAL_GetTick();
    elapsed_ms = now_ms - s_app_light_ctx.mode_start_tick_ms;

    if (s_app_light_ctx.mode == APP_LIGHT_MODE_IDENTIFY)
    {
        if (elapsed_ms >= s_app_light_ctx.identify_duration_ms)
        {
            s_app_light_ctx.mode = s_app_light_ctx.base_mode;
            s_app_light_ctx.mode_start_tick_ms = now_ms;
            elapsed_ms = 0u;
        }
    }

    if (s_app_light_ctx.mode == APP_LIGHT_MODE_IDENTIFY)
    {
        led_enabled = app_light_calc_identify_enabled(elapsed_ms);
    }
    else
    {
        led_enabled = app_light_calc_base_enabled(s_app_light_ctx.mode, elapsed_ms);
    }

    app_light_write_led(led_enabled);
}
