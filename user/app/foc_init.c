#include "foc_init.h"
#include "foc/foc_core.h"
#include "foc/foc_math.h"

#include "as5600.h"
#include "flash.h"
#include "log.h"
#include "can.h"
#include "can_protocol.h"
#include "app_light.h"

#include "stm32f1xx_hal.h"          // HAL库核心头文件
#include "stm32f1xx_hal_tim.h"      // 定时器HAL库
#include "stm32f1xx_hal_gpio.h"     // GPIO HAL库
#include "stm32f1xx_hal_rcc.h"      // 时钟HAL库
#include <stdio.h>

//extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;

volatile uint8_t updata_flag = 0U;


static void GPIO_Init(void);
static int32_t foc_limit_target(int32_t target);
static uint16_t foc_calc_loop_div(uint16_t target_hz);
static void foc_apply_config(const user_info_foc_config_t *config);
static void foc_map_phase_output(uint16_t a,
                                 uint16_t b,
                                 uint16_t c,
                                 uint16_t *ch1,
                                 uint16_t *ch2,
                                 uint16_t *ch3);

#define FOC_SCHEDULER_TICK_HZ      4000U
#define FOC_SPEED_LOOP_HZ          100U
#define FOC_POSITION_LOOP_HZ       25U

/**
 * @brief 输出三相 PWM 占空比。
 * @param a A 相占空比。
 * @param b B 相占空比。
 * @param c C 相占空比。
 * @return void
 */
void foc_output(uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t ch1;
    uint16_t ch2;
    uint16_t ch3;

    foc_map_phase_output(a, b, c, &ch1, &ch2, &ch3);

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ch1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ch2);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, ch3);
}

foc_t foc;
static foc_ctrl_mode_t foc_control_mode = FOC_CTRL_MODE_SPEED;
static int32_t foc_current_target = 0;

foc_cfg_t cfg = {

	.pole_pairs = USER_INFO_DEFAULT_POLE_PAIRS,
	.master_voltage = USER_INFO_DEFAULT_MASTER_VOLTAGE_MV,

	.control_hz = USER_INFO_DEFAULT_CONTROL_HZ,
	.sensor_hz = USER_INFO_DEFAULT_SENSOR_HZ,
    .phase_map = USER_INFO_DEFAULT_PHASE_MAP,

	.pwm_period = 1600,
	.pwm_hz = 20000,

    .output = foc_output,
    .delay = HAL_Delay,
    .get_angle_rad = as5600GetAngleRadians,
    .get_time = HAL_GetTick,
};

/**
 * @brief 执行零点重新标定。
 * @return int32_t 标定后的零点角度，单位为 mrad。
 */
int32_t foc_zero_angle_reset(void)
{
    foc.state = Foc_Zero_Angle_Init;
    return foc_zero_reset(&foc);
}

/**
 * @brief 设置电机零点角度。
 * @param angle 新的零点角度，单位为 mrad。
 * @return void
 */
void foc_set_zero_angle(int32_t angle)
{
    foc.angle.zero_angle = angle;
}

/**
 * @brief 获取当前保存的零点角度。
 * @return int32_t 当前零点角度，单位为 mrad。
 */
int32_t foc_get_zero_angle(void)
{
    return foc.angle.zero_angle;
}

/**
 * @brief 设置当前 FOC 控制模式。
 * @param mode 目标控制模式。
 * @return void
 */
void foc_control_mode_set(foc_ctrl_mode_t mode)
{
    foc_control_mode = mode;

    if (mode == FOC_CTRL_MODE_CURRENT)
    {
        foc_set_target(0, foc_current_target, 0);
    }
}

/**
 * @brief 获取当前 FOC 控制模式。
 * @return foc_ctrl_mode_t 当前控制模式。
 */
foc_ctrl_mode_t foc_control_mode_get(void)
{
    return foc_control_mode;
}

/**
 * @brief 限制目标值的输出范围。
 * @param target 待限制的目标值。
 * @return int32_t 限幅后的目标值。
 * @note 目标值会被限制到 [-OUT_MAX, OUT_MAX] 区间。
 */
static int32_t foc_limit_target(int32_t target)
{
    if (target > OUT_MAX)
    {
        return OUT_MAX;
    }

    if (target < (-OUT_MAX))
    {
        return (-OUT_MAX);
    }

    return target;
}

/**
 * @brief 根据目标频率计算调度分频。
 * @param target_hz 目标频率，单位：Hz。
 * @return uint16_t 调度分频值，单位：tick。
 * @note 实际频率不会高于 FOC_SCHEDULER_TICK_HZ，无法整除时向下取最接近的可实现频率。
 */
static uint16_t foc_calc_loop_div(uint16_t target_hz)
{
    uint32_t div;

    if (target_hz == 0U)
    {
        return 1U;
    }

    if (target_hz >= FOC_SCHEDULER_TICK_HZ)
    {
        return 1U;
    }

    div = (FOC_SCHEDULER_TICK_HZ + target_hz - 1U) / target_hz;
    if (div == 0U)
    {
        return 1U;
    }

    if (div > UINT16_MAX)
    {
        return UINT16_MAX;
    }

    return (uint16_t)div;
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
static void foc_map_phase_output(uint16_t a,
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

    switch (cfg.phase_map)
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
static void foc_apply_config(const user_info_foc_config_t *config)
{
    user_info_foc_config_t normalized_config;

    if (config == NULL)
    {
        return;
    }

    normalized_config = *config;
    UserInfo_NormalizeFocConfig(&normalized_config);
    cfg.pole_pairs = (uint8_t)normalized_config.pole_pairs;
    cfg.master_voltage = (int32_t)normalized_config.master_voltage_mv;
    cfg.control_hz = (uint16_t)normalized_config.control_hz;
    cfg.sensor_hz = (uint16_t)normalized_config.sensor_hz;
    cfg.phase_map = (uint8_t)normalized_config.phase_map;
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

    foc_apply_config(config);
    foc.info.master_voltage = cfg.master_voltage;
    foc.info.vector_voltage = OUT_MAX;
    foc.info.pole_pairs = cfg.pole_pairs;
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

    config->pole_pairs = cfg.pole_pairs;
    config->master_voltage_mv = (uint32_t)cfg.master_voltage;
    config->control_hz = cfg.control_hz;
    config->sensor_hz = cfg.sensor_hz;
    config->phase_map = cfg.phase_map;
    return USER_INFO_OK;
}

foc_pid_t angle_pid = {

    .target = 0,
    .p = 0.0f,
    .out_max = 0
};

foc_pid_t speed_pid = {

    .p = 0,
    .i = 0,
    .i_acc_max = 0,
    .out_max = 0,
};

#define EXT_SPEED_LEN 20
uint16_t angle_recoder[EXT_SPEED_LEN];
uint8_t angle_idex = 0;
uint8_t angle_first_idex = 0;
uint8_t angle_init = 0;
float speed = 0;

/**
 * @brief 执行 FOC 周期调度。
 * @return int32_t 处理了一个调度 tick 返回 1，否则返回 0。
 * @note TIM2 基准频率为 4000Hz，控制环和传感器采样由 cfg.control_hz 与 cfg.sensor_hz 决定。
 */
int32_t foc_updata(void)
{
    static uint16_t scheduler_tick = 0;
    const uint16_t control_div = foc_calc_loop_div(foc.cfg->control_hz);
    const uint16_t sensor_div = foc_calc_loop_div(foc.cfg->sensor_hz);
    const uint16_t speed_loop_div = foc_calc_loop_div(FOC_SPEED_LOOP_HZ);
    const uint16_t position_loop_div = foc_calc_loop_div(FOC_POSITION_LOOP_HZ);
    uint8_t control_loop_due = 0U;
    uint8_t sensor_loop_due = 0U;
    uint8_t speed_loop_due = 0U;
    uint8_t position_loop_due = 0U;

    if (updata_flag == 0U)
    {
        return 0;
    }

    scheduler_tick++;

    control_loop_due = ((scheduler_tick % control_div) == 0U);
    sensor_loop_due = ((scheduler_tick % sensor_div) == 0U);
    speed_loop_due = ((scheduler_tick % speed_loop_div) == 0U);
    position_loop_due = ((scheduler_tick % position_loop_div) == 0U);

    if (sensor_loop_due != 0U)
    {
        foc_sensor_updata(&foc);

        angle_recoder[angle_idex] = foc_get_angle(&foc);

        if (angle_init == 0U)
        {
            if (angle_idex == 0U)
            {
                speed = 0.0f;
            }
            else
            {
                int16_t speed_delta = angle_recoder[angle_idex] - angle_recoder[0];

                if (speed_delta > FOC_PIx1000)
                {
                    speed_delta -= 2 * FOC_PIx1000;
                }
                else
                {
                    if (speed_delta < (-FOC_PIx1000))
                    {
                        speed_delta += 2 * FOC_PIx1000;
                    }
                }
                speed = (float)speed_delta / angle_idex * 2.0f;
            }

            if (angle_idex == (EXT_SPEED_LEN - 2U))
            {
                angle_init = 1U;
            }
        }
        else
        {
            int16_t speed_delta;

            angle_first_idex = angle_idex + 1U;

            if (angle_first_idex == EXT_SPEED_LEN)
            {
                angle_first_idex = 0U;
            }

            speed_delta = angle_recoder[angle_idex] - angle_recoder[angle_first_idex];

            if (speed_delta > FOC_PIx1000)
            {
                speed_delta -= 2 * FOC_PIx1000;
            }
            else
            {
                if (speed_delta < (-FOC_PIx1000))
                {
                    speed_delta += 2 * FOC_PIx1000;
                }
            }
            speed = (float)speed_delta / EXT_SPEED_LEN * 2.0f;
        }

        angle_idex++;

        if (angle_idex == EXT_SPEED_LEN)
        {
            angle_idex = 0U;
        }
    }

    if (speed_loop_due != 0U)
    {
        if (foc_control_mode != FOC_CTRL_MODE_CURRENT)
        {
            int32_t out = 0;

            foc_percent_update(&speed_pid, speed);
            out = (int32_t)pid_speed_ctrl(&speed_pid);
            foc_set_target(0, out, 0);
        }

        foc_speed_updata(&foc);
        can_protocol_report_motor_state();
    }

    if ((position_loop_due != 0U)
        && (foc_control_mode == FOC_CTRL_MODE_POSITION))
    {
        float out = 0.0f;

        foc_percent_update(&angle_pid, foc_get_angle(&foc));
        out = pid_angle_ctrl(&angle_pid);
        foc_speed_pid_set_target(out);
    }

    if (control_loop_due != 0U)
    {
        foc_control(&foc);
    }

    if (scheduler_tick >= FOC_SCHEDULER_TICK_HZ)
    {
        scheduler_tick = 0U;
    }

    updata_flag = 0U;

    return 1;
}

/**
 * @brief 初始化 FOC 运行环境。
 * @return void
 */
/**
 * @brief 判断当前是否存在待处理的 FOC 调度请求。
 * @return int 存在待处理请求返回 1，否则返回 0。
 */
/**
 * @brief 判断当前是否存在待处理的 FOC 调度请求。
 * @return int 存在待处理请求返回 1，否则返回 0。
 */
int foc_update_is_pending(void)
{
    if (updata_flag != 0U)
    {
        return 1;
    }

    return 0;
}

void foc_root_init(void)
{
    user_info_foc_config_t foc_config;

    if (Flash_LoadFocConfig(&foc_config) == USER_INFO_OK)
    {
        foc_apply_config(&foc_config);
    }

    foc_init(&foc,&cfg);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_ENABLE(&htim1);

    GPIO_Init();

    foc_output_enable(1);

    recoder_data data = {0};
    if(Load_Recoder(&data))
    {
        foc_zero_reset_manual(&foc, data.calibration_angle);
        printf("zero angle loaded: %d\n", data.calibration_angle);
    }
    else
    {
        data.calibration_angle = foc_zero_angle_reset();
        data.can_id = USER_INFO_MANAGE_CAN_ID;
        data.ota = USER_INFO_OTA_FLAG_APP;
        data.can_configured = USER_INFO_CAN_CONFIGURED_NO;
        data.report_enabled = USER_INFO_DEFAULT_REPORT_ENABLE;
        data.report_period_ms = USER_INFO_DEFAULT_REPORT_PERIOD_MS;
        data.pole_pairs = USER_INFO_DEFAULT_POLE_PAIRS;
        data.master_voltage_mv = USER_INFO_DEFAULT_MASTER_VOLTAGE_MV;
        data.control_hz = USER_INFO_DEFAULT_CONTROL_HZ;
        data.sensor_hz = USER_INFO_DEFAULT_SENSOR_HZ;
        data.phase_map = USER_INFO_DEFAULT_PHASE_MAP;
        Save_Recoder(data);
        printf("zero angle saved: %d\n", data.calibration_angle);
    }

    HAL_TIM_Base_Start_IT(&htim2);
}



/**
 * @brief 设置电流模式下的 q 轴目标值。
 * @param target 电流目标值，对应 q 轴内部控制量。
 * @return int32_t 实际生效的目标值。
 * @note 当当前模式为电流模式时，会立即刷新到 FOC 目标输出。
 */
int32_t foc_current_set_target(int32_t target)
{
    foc_current_target = foc_limit_target(target);

    if (foc_control_mode == FOC_CTRL_MODE_CURRENT)
    {
        foc_set_target(0, foc_current_target, 0);
    }

    return foc_current_target;
}

/**
 * @brief 设置 FOC 的 d 轴、q 轴和电角度目标。
 * @param d_target d 轴目标值。
 * @param q_target q 轴目标值。
 * @param theta_target 电角度目标值。
 * @return void
 */
void foc_set_target(int32_t d_target, int32_t q_target, int32_t theta_target)
{
    foc_park_t target = {
        .d = foc_limit_target(d_target),
        .q = foc_limit_target(q_target),
        .theta = theta_target,
    };

    foc_target_updata(&foc, target);
}

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
 * @brief 定时器周期回调函数。
 * @param htim 定时器句柄指针。
 * @return void
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        AppLight_Poll();
        updata_flag = 1;
    }
}

//GPIO_PIN_14 Logic high enables OUT. Internalpulldown
//GPIO_PIN_3 Active-low reset input initializesinternal logicanddisablesthe
static void GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_14 | GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}


/**
 * @brief 设置速度环 PID 参数。
 * @param p 比例系数。
 * @param i 积分系数。
 * @param i_out_max 积分累加上限。
 * @param out_max 输出上限。
 * @return void
 */
void foc_speed_pid_set_param(float p, float i, float i_out_max, float out_max)
{
    foc_speed_pid_set_full_param(p, i, 0.0f, i_out_max, out_max);
}

/**
 * @brief 设置速度环完整 PID 参数。
 * @param p 比例系数。
 * @param i 积分系数。
 * @param d 微分系数。
 * @param i_acc_max 积分累加上限。
 * @param out_max 输出上限。
 * @return void
 */
void foc_speed_pid_set_full_param(float p, float i, float d, float i_acc_max, float out_max)
{
    if (i_acc_max > OUT_MAX)
    {
        i_acc_max = OUT_MAX;
    }

    if (out_max > OUT_MAX)
    {
        out_max = OUT_MAX;
    }

    pid_set_param(&speed_pid, p, i, d, i_acc_max, out_max);
}

/**
 * @brief 获取速度环 PID 参数。
 * @param p 比例系数输出指针。
 * @param i 积分系数输出指针。
 * @param i_acc_max 积分累加上限输出指针。
 * @param out_max 输出上限输出指针。
 * @return void
 */
void foc_speed_pid_get_param(float *p, float *i, float *i_acc_max, float *out_max)
{
    foc_speed_pid_get_full_param(p, i, NULL, i_acc_max, out_max);
}

/**
 * @brief 获取速度环完整 PID 参数。
 * @param p 比例系数输出指针。
 * @param i 积分系数输出指针。
 * @param d 微分系数输出指针。
 * @param i_acc_max 积分累加上限输出指针。
 * @param out_max 输出上限输出指针。
 * @return void
 */
void foc_speed_pid_get_full_param(float *p, float *i, float *d, float *i_acc_max, float *out_max)
{
    if (p != NULL)
    {
        *p = speed_pid.p;
    }

    if (i != NULL)
    {
        *i = speed_pid.i;
    }

    if (d != NULL)
    {
        *d = speed_pid.d;
    }

    if (i_acc_max != NULL)
    {
        *i_acc_max = speed_pid.i_acc_max;
    }

    if (out_max != NULL)
    {
        *out_max = speed_pid.out_max;
    }
}

/**
 * @brief 设置速度环目标值。
 * @param target 目标速度，单位为 mrad/s。
 * @return void
 */
void foc_speed_pid_set_target(float target)
{
    pid_set_target(&speed_pid, target);
}

/**
 * @brief 获取速度环目标值。
 * @param target 目标速度输出指针。
 * @return void
 */
void foc_speed_pid_get_target(float *target)
{
    *target = speed_pid.target;
}

/**
 * @brief 获取当前速度估计值。
 * @return int32_t 当前速度估计值，单位为 mrad/s。
 */
int32_t foc_get_speed_estimate(void)
{
    return (int32_t)speed;
}

/**
 * @brief 设置位置环 PID 参数。
 * @param p 比例系数。
 * @param i 积分系数。
 * @param d 微分系数。
 * @param i_acc_max 积分累加上限。
 * @param out_max 输出上限。
 * @return void
 */
void foc_position_pid_set_param(float p, float i, float d, float i_acc_max, float out_max)
{
    if (i_acc_max > OUT_MAX)
    {
        i_acc_max = OUT_MAX;
    }

    if (out_max > OUT_MAX)
    {
        out_max = OUT_MAX;
    }

    pid_set_param(&angle_pid, p, i, d, i_acc_max, out_max);
}

/**
 * @brief 获取位置环 PID 参数。
 * @param p 比例系数输出指针。
 * @param i 积分系数输出指针。
 * @param d 微分系数输出指针。
 * @param i_acc_max 积分累加上限输出指针。
 * @param out_max 输出上限输出指针。
 * @return void
 */
void foc_position_pid_get_param(float *p, float *i, float *d, float *i_acc_max, float *out_max)
{
    if (p != NULL)
    {
        *p = angle_pid.p;
    }

    if (i != NULL)
    {
        *i = angle_pid.i;
    }

    if (d != NULL)
    {
        *d = angle_pid.d;
    }

    if (i_acc_max != NULL)
    {
        *i_acc_max = angle_pid.i_acc_max;
    }

    if (out_max != NULL)
    {
        *out_max = angle_pid.out_max;
    }
}

/**
 * @brief 设置位置环目标值。
 * @param target 目标位置，单位为 mrad。
 * @return void
 */
void foc_position_pid_set_target(float target)
{
    pid_set_target(&angle_pid, target);
}

/**
 * @brief 获取位置环目标值。
 * @param target 目标位置输出指针。
 * @return void
 */
void foc_position_pid_get_target(float *target)
{
    *target = angle_pid.target;
}
