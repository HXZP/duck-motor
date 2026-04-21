#include "foc_init.h"
#include "foc/foc_core.h"
#include "foc/foc_math.h"

#include "as5600.h"
#include "flash.h"
#include "log.h"
#include "can.h"
#include "can_protocol.h"

#include "stm32f1xx_hal.h"          // HAL库核心头文件
#include "stm32f1xx_hal_tim.h"      // 定时器HAL库
#include "stm32f1xx_hal_gpio.h"     // GPIO HAL库
#include "stm32f1xx_hal_rcc.h"      // 时钟HAL库
#include <stdio.h>

//extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;

uint8_t updata_flag = 0;


static void GPIO_Init(void);
static int32_t foc_limit_target(int32_t target);

/**
 * @brief 输出三相 PWM 占空比。
 * @param a A 相占空比。
 * @param b B 相占空比。
 * @param c C 相占空比。
 * @return void
 */
void foc_output(uint16_t a, uint16_t b, uint16_t c)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, a);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, b);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, c);
}

foc_t foc;
static foc_ctrl_mode_t foc_control_mode = FOC_CTRL_MODE_SPEED;
static int32_t foc_current_target = 0;

foc_cfg_t cfg = {

	.pole_pairs = 7,
	.master_voltage = 12000,
	.pwm_period = 1600,
	
	.pwm_hz = 20000,
	.control_hz = 1000,
    .control_khz = 1,
	.sensor_hz = 5000,
    
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
 * @brief Run the FOC scheduler on each TIM2 base tick.
 * @return int32_t Returns 1 when one scheduler tick is processed, otherwise 0.
 * @note This function assumes a 250 us TIM2 base period. The sensor update
 *       runs every 2 ticks, the speed loop runs every 40 ticks, and the
 *       position loop runs every 160 ticks.
 */
int32_t foc_updata(void)
{
    static uint16_t flag_250us = 0;
    const uint16_t sensor_div = 2U;
    const uint16_t speed_loop_div = 40U;
    const uint16_t position_loop_div = 160U;
    uint8_t sensor_loop_due = 0U;
    uint8_t speed_loop_due = 0U;
    uint8_t position_loop_due = 0U;

    if (updata_flag == 0U)
    {
        return 0;
    }

    flag_250us++;

    sensor_loop_due = ((flag_250us % sensor_div) == 0U);
    speed_loop_due = ((flag_250us % speed_loop_div) == 0U);
    position_loop_due = ((flag_250us % position_loop_div) == 0U);
        
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

    foc_control(&foc);

    if (flag_250us >= (40U * 1600U))
    {
        flag_250us = 0U;
    }

    updata_flag = 0U;

    return 1;
}

/**
 * @brief 初始化 FOC 运行环境。
 * @return void
 */
void foc_root_init(void)
{
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
        data.can_id = CAN_PROTOCOL_DEFAULT_NODE_ID;
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
    if(enable)
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

uint16_t led_cnt = 0;
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        
        led_cnt++;

        if(led_cnt <= 700)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,0);
        }
        else if(led_cnt <= 800)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,1);
        }   
        else if(led_cnt <= 900)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,0);
        }
        else if(led_cnt <= 1000)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,1);
        }
        else if(led_cnt <= 1100)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,0);
        }
        else if(led_cnt < 1500)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,1);
        }
        else if(led_cnt == 1500)
        {
            led_cnt = 0;
        }        
        
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

