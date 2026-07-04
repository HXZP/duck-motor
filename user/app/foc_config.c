#include "app/foc_config.h"

#include "app/as5600.h"
#include "app/flash.h"
#include "app/foc_app.h"

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_rcc.h"
#include "stm32f1xx_hal_tim.h"

extern TIM_HandleTypeDef htim1;

/**
 * @brief 初始化 FOC 输出使能 GPIO。
 * @return void
 */
static void foc_config_gpio_init(void);

/**
 * @brief 输出三相 PWM 占空比。
 * @param a A 相占空比。
 * @param b B 相占空比。
 * @param c C 相占空比。
 * @return void
 */
static void foc_config_output(uint16_t a, uint16_t b, uint16_t c);

/**
 * @brief 按相序映射计算 TIM 物理通道占空比。
 * @param a A 相占空比，单位：计数。
 * @param b B 相占空比，单位：计数。
 * @param c C 相占空比，单位：计数。
 * @param ch1 TIM1 CH1 占空比输出指针，单位：计数。
 * @param ch2 TIM1 CH2 占空比输出指针，单位：计数。
 * @param ch3 TIM1 CH3 占空比输出指针，单位：计数。
 * @return void
 */
static void foc_config_map_phase_output(uint16_t a,
                                        uint16_t b,
                                        uint16_t c,
                                        uint16_t *ch1,
                                        uint16_t *ch2,
                                        uint16_t *ch3);

/**
 * @brief 应用 FOC 基础配置到运行配置。
 * @param config FOC 基础配置。
 * @return void
 */
static void foc_config_apply_runtime(const user_info_foc_config_t *config);

foc_cfg_t foc_runtime_config = {
    .pole_pairs = USER_INFO_DEFAULT_POLE_PAIRS,
    .master_voltage = USER_INFO_DEFAULT_MASTER_VOLTAGE_MV,
    .control_hz = USER_INFO_DEFAULT_CONTROL_HZ,
    .sensor_hz = USER_INFO_DEFAULT_SENSOR_HZ,
    .phase_map = USER_INFO_DEFAULT_PHASE_MAP,
    .pwm_period = 1600,
    .pwm_hz = 20000,
    .output = foc_config_output,
    .delay = HAL_Delay,
    .get_angle_rad = as5600GetAngleRadians,
    .get_time = HAL_GetTick,
};

/**
 * @brief 从 Flash 加载 FOC 基础配置并应用到运行配置。
 * @return void
 */
void foc_config_load_saved(void)
{
    user_info_foc_config_t foc_config;

    if (Flash_LoadFocConfig(&foc_config) == USER_INFO_OK)
    {
        foc_config_apply_runtime(&foc_config);
    }
}

/**
 * @brief 启动 FOC PWM 输出相关硬件。
 * @return void
 */
void foc_config_start_pwm(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_ENABLE(&htim1);

    foc_config_gpio_init();
    foc_output_enable(1U);
}

/**
 * @brief 使能或关闭功率输出。
 * @param enable 输出使能标志，0 表示关闭，非 0 表示使能。
 * @return void
 */
void foc_output_enable(uint8_t enable)
{
    if (enable != 0U)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
    }
}

/**
 * @brief 设置并应用 FOC 基础配置。
 * @param config FOC 基础配置。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int foc_config_set(const user_info_foc_config_t *config)
{
    int ret;

    ret = UserInfo_ValidateFocConfig(config);
    if (ret != USER_INFO_OK)
    {
        return ret;
    }

    foc_config_apply_runtime(config);
    foc.info.master_voltage = foc_runtime_config.master_voltage;
    foc.info.vector_voltage = OUT_MAX;
    foc.info.pole_pairs = foc_runtime_config.pole_pairs;

    return USER_INFO_OK;
}

/**
 * @brief 获取当前 FOC 基础配置。
 * @param config FOC 基础配置输出缓冲区。
 * @return int 成功返回 USER_INFO_OK，失败返回 USER_INFO_ERR_xxx。
 */
int foc_config_get(user_info_foc_config_t *config)
{
    if (config == NULL)
    {
        return USER_INFO_ERR_PARAM;
    }

    config->pole_pairs = foc_runtime_config.pole_pairs;
    config->master_voltage_mv = (uint32_t)foc_runtime_config.master_voltage;
    config->control_hz = foc_runtime_config.control_hz;
    config->sensor_hz = foc_runtime_config.sensor_hz;
    config->phase_map = foc_runtime_config.phase_map;

    return USER_INFO_OK;
}

/**
 * @brief 输出三相 PWM 占空比。
 * @param a A 相占空比。
 * @param b B 相占空比。
 * @param c C 相占空比。
 * @return void
 */
static void foc_config_output(uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t ch1;
    uint16_t ch2;
    uint16_t ch3;

    foc_config_map_phase_output(a, b, c, &ch1, &ch2, &ch3);

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ch1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ch2);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, ch3);
}

/**
 * @brief 按相序映射计算 TIM 物理通道占空比。
 * @param a A 相占空比，单位：计数。
 * @param b B 相占空比，单位：计数。
 * @param c C 相占空比，单位：计数。
 * @param ch1 TIM1 CH1 占空比输出指针，单位：计数。
 * @param ch2 TIM1 CH2 占空比输出指针，单位：计数。
 * @param ch3 TIM1 CH3 占空比输出指针，单位：计数。
 * @return void
 */
static void foc_config_map_phase_output(uint16_t a,
                                        uint16_t b,
                                        uint16_t c,
                                        uint16_t *ch1,
                                        uint16_t *ch2,
                                        uint16_t *ch3)
{
    if ((ch1 == NULL) || (ch2 == NULL) || (ch3 == NULL))
    {
        return;
    }

    switch (foc_runtime_config.phase_map)
    {
        case 1u:
        {
            *ch1 = a;
            *ch2 = c;
            *ch3 = b;
            break;
        }

        case 2u:
        {
            *ch1 = b;
            *ch2 = a;
            *ch3 = c;
            break;
        }

        case 3u:
        {
            *ch1 = b;
            *ch2 = c;
            *ch3 = a;
            break;
        }

        case 4u:
        {
            *ch1 = c;
            *ch2 = a;
            *ch3 = b;
            break;
        }

        case 5u:
        {
            *ch1 = c;
            *ch2 = b;
            *ch3 = a;
            break;
        }

        case 0u:
        default:
        {
            *ch1 = a;
            *ch2 = b;
            *ch3 = c;
            break;
        }
    }
}

/**
 * @brief 应用 FOC 基础配置到运行配置。
 * @param config FOC 基础配置。
 * @return void
 */
static void foc_config_apply_runtime(const user_info_foc_config_t *config)
{
    user_info_foc_config_t normalized_config;

    if (config == NULL)
    {
        return;
    }

    normalized_config = *config;
    UserInfo_NormalizeFocConfig(&normalized_config);
    foc_runtime_config.pole_pairs = (uint8_t)normalized_config.pole_pairs;
    foc_runtime_config.master_voltage = (int32_t)normalized_config.master_voltage_mv;
    foc_runtime_config.control_hz = (uint16_t)normalized_config.control_hz;
    foc_runtime_config.sensor_hz = (uint16_t)normalized_config.sensor_hz;
    foc_runtime_config.phase_map = (uint8_t)normalized_config.phase_map;
}

/**
 * @brief 初始化 FOC 输出使能 GPIO。
 * @return void
 */
static void foc_config_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_14 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}
