#include "app/foc_app.h"

#include "app/app_error.h"
#include "app/app_light.h"
#include "app/can_protocol.h"
#include "app/flash.h"
#include "app/foc_config.h"
#include "foc/foc_math.h"

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_tim.h"

#include <stddef.h>
#include <stdio.h>

extern TIM_HandleTypeDef htim2;

static volatile uint8_t foc_update_pending_flag = 0U;

#define FOC_SCHEDULER_TICK_HZ      4000U
#define FOC_SPEED_LOOP_HZ          500U
#define FOC_POSITION_LOOP_HZ       100U
#define FOC_SPEED_SAMPLE_WINDOW    20U
#define FOC_MECHANICAL_CYCLE_MRAD  6283
#define FOC_TRACE_SAMPLE_COUNT     512U
#define FOC_TRACE_TRIGGER_SPEED    10000
#define FOC_TRACE_POST_SAMPLES     384U
#define FOC_TRACE_STATE_IDLE       0U
#define FOC_TRACE_STATE_ARMED      1U
#define FOC_TRACE_STATE_POST       2U
#define FOC_TRACE_STATE_FROZEN     3U

foc_t foc;
static foc_ctrl_mode_t foc_control_mode = FOC_CTRL_MODE_SPEED;
static int32_t foc_current_target = 0;

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

static foc_pid_t angle_pid = {

    .target = 0,
    .p = 0.0f,
    .out_max = 0
};

static foc_pid_t speed_pid = {

    .p = 0,
    .i = 0,
    .i_acc_max = 0,
    .out_max = 0,
};

static int64_t foc_speed_position_samples[FOC_SPEED_SAMPLE_WINDOW];
static uint32_t foc_speed_time_samples[FOC_SPEED_SAMPLE_WINDOW];
static uint8_t foc_speed_sample_index = 0U;
static uint8_t foc_speed_sample_count = 0U;
static uint8_t foc_speed_estimator_initialized = 0U;
static int32_t foc_speed_previous_angle = 0;
static int64_t foc_speed_continuous_position = 0;
static float foc_speed_estimate = 0.0f;
static foc_trace_sample_t foc_trace_samples[FOC_TRACE_SAMPLE_COUNT];
static uint16_t foc_trace_write_index = 0U;
static uint16_t foc_trace_sample_count = 0U;
static uint16_t foc_trace_post_remaining = 0U;
static uint8_t foc_trace_state = FOC_TRACE_STATE_IDLE;

/**
 * @brief 将角度归一化到一个机械周期。
 * @param angle 待归一化角度，单位：mrad。
 * @return uint16_t 归一化角度，单位：mrad。
 */
static uint16_t foc_trace_normalize_angle(int32_t angle)
{
    int32_t normalized = angle % FOC_MECHANICAL_CYCLE_MRAD;

    if (normalized < 0)
    {
        normalized += FOC_MECHANICAL_CYCLE_MRAD;
    }

    return (uint16_t)normalized;
}

/**
 * @brief 在传感器更新周期记录一条 FOC 高速采样。
 * @return void
 */
static void foc_trace_record_sample(void)
{
    foc_trace_sample_t *sample;

    if ((foc_trace_state != FOC_TRACE_STATE_ARMED)
        && (foc_trace_state != FOC_TRACE_STATE_POST))
    {
        return;
    }

    sample = &foc_trace_samples[foc_trace_write_index];
    sample->timestamp_ms = (uint16_t)foc.cfg->get_time();
    sample->sensor_angle = foc_trace_normalize_angle(foc.angle.sensor_angle);
    sample->mechanical_angle = foc_trace_normalize_angle(foc.angle.mech_angle);
    sample->electrical_angle = foc_trace_normalize_angle(foc.angle.elec_angle);
    sample->speed = (int32_t)foc_speed_estimate;
    sample->q_target = (int16_t)foc.target_park.q;
    sample->pwm_a = (uint8_t)(((uint32_t)foc.pwm_duty.a * 255U) / foc.pwm_duty.T);
    sample->pwm_b = (uint8_t)(((uint32_t)foc.pwm_duty.b * 255U) / foc.pwm_duty.T);
    sample->pwm_c = (uint8_t)(((uint32_t)foc.pwm_duty.c * 255U) / foc.pwm_duty.T);
    sample->reserved = 0U;

    foc_trace_write_index++;
    if (foc_trace_write_index >= FOC_TRACE_SAMPLE_COUNT)
    {
        foc_trace_write_index = 0U;
    }

    if (foc_trace_sample_count < FOC_TRACE_SAMPLE_COUNT)
    {
        foc_trace_sample_count++;
    }

    if (foc_trace_state == FOC_TRACE_STATE_ARMED)
    {
        if ((foc_speed_estimate >= (float)FOC_TRACE_TRIGGER_SPEED)
            || (foc_speed_estimate <= (float)(-FOC_TRACE_TRIGGER_SPEED)))
        {
            foc_trace_state = FOC_TRACE_STATE_POST;
            foc_trace_post_remaining = FOC_TRACE_POST_SAMPLES;
        }
    }
    else
    {
        if (foc_trace_post_remaining > 0U)
        {
            foc_trace_post_remaining--;
        }

        if (foc_trace_post_remaining == 0U)
        {
            foc_trace_state = FOC_TRACE_STATE_FROZEN;
        }
    }
}

/**
 * @brief 根据角度差和实际时间间隔计算机械速度。
 * @param delta 机械角度差，单位：mrad。
 * @param elapsed_ms 实际时间间隔，单位：ms。
 * @return float 机械速度，单位：mrad/s。
 */
static float foc_calc_speed_mrad_s(int64_t delta, uint32_t elapsed_ms)
{
    if (elapsed_ms == 0U)
    {
        return 0.0f;
    }

    return ((float)delta * 1000.0f) / (float)elapsed_ms;
}

/**
 * @brief 计算跨零后的机械角度差。
 * @param current_angle 当前机械角度，单位：mrad。
 * @param previous_angle 历史机械角度，单位：mrad。
 * @return int32_t 归一化后的角度差，单位：mrad。
 */
static int32_t foc_calc_angle_delta(int32_t current_angle, int32_t previous_angle)
{
    int32_t delta;

    delta = current_angle - previous_angle;

    if (delta > FOC_PIx1000)
    {
        delta -= FOC_MECHANICAL_CYCLE_MRAD;
    }
    else
    {
        if (delta < (-FOC_PIx1000))
        {
            delta += FOC_MECHANICAL_CYCLE_MRAD;
        }
    }

    return delta;
}

/**
 * @brief 根据传感器采样历史更新速度估计值。
 * @return void
 * @note 相邻采样负责角度解包，连续角度窗口负责降低量化噪声。
 * @note 当前窗口为 20 个传感器采样点；默认 2000Hz 采样时，估计窗口约为 10ms。
 */
static void foc_update_speed_estimate(void)
{
    int32_t current_angle;
    int32_t sample_delta;
    int64_t position_delta;
    uint32_t current_time_ms;
    uint32_t elapsed_ms;
    uint8_t history_index;

    current_angle = foc_get_angle(&foc);
    current_time_ms = foc.cfg->get_time();

    if (foc_speed_estimator_initialized == 0U)
    {
        foc_speed_estimator_initialized = 1U;
        foc_speed_previous_angle = current_angle;
        foc_speed_continuous_position = 0;
        foc_speed_position_samples[0] = foc_speed_continuous_position;
        foc_speed_time_samples[0] = current_time_ms;
        foc_speed_sample_index = 1U;
        foc_speed_sample_count = 1U;
        foc_speed_estimate = 0.0f;
        return;
    }

    sample_delta = foc_calc_angle_delta(current_angle, foc_speed_previous_angle);
    foc_speed_previous_angle = current_angle;
    foc_speed_continuous_position += sample_delta;

    if (foc_speed_sample_count < FOC_SPEED_SAMPLE_WINDOW)
    {
        foc_speed_position_samples[foc_speed_sample_index] = foc_speed_continuous_position;
        foc_speed_time_samples[foc_speed_sample_index] = current_time_ms;
        position_delta = foc_speed_continuous_position - foc_speed_position_samples[0];
        elapsed_ms = current_time_ms - foc_speed_time_samples[0];
        foc_speed_estimate = foc_calc_speed_mrad_s(position_delta, elapsed_ms);

        foc_speed_sample_count++;
        foc_speed_sample_index++;

        if (foc_speed_sample_index == FOC_SPEED_SAMPLE_WINDOW)
        {
            foc_speed_sample_index = 0U;
        }

        return;
    }

    history_index = foc_speed_sample_index + 1U;

    if (history_index == FOC_SPEED_SAMPLE_WINDOW)
    {
        history_index = 0U;
    }

    foc_speed_position_samples[foc_speed_sample_index] = foc_speed_continuous_position;
    foc_speed_time_samples[foc_speed_sample_index] = current_time_ms;
    position_delta = foc_speed_continuous_position - foc_speed_position_samples[history_index];
    elapsed_ms = current_time_ms - foc_speed_time_samples[history_index];
    foc_speed_estimate = foc_calc_speed_mrad_s(position_delta, elapsed_ms);
    foc_speed_sample_index = history_index;
}

/**
 * @brief 执行速度环调度。
 * @return void
 * @note 电流模式下跳过速度 PID，避免覆盖 CAN 下发的电流目标。
 */
static void foc_run_speed_loop(void)
{
    if (foc_control_mode != FOC_CTRL_MODE_CURRENT)
    {
        int32_t out = 0;

        foc_percent_update(&speed_pid, foc_speed_estimate);
        out = (int32_t)pid_speed_ctrl(&speed_pid);
        foc_set_target(0, out, 0);
    }

    foc_speed_updata(&foc);
}

/**
 * @brief 执行位置环调度。
 * @return void
 */
static void foc_run_position_loop(void)
{
    float out = 0.0f;

    foc_percent_update(&angle_pid, foc_get_angle(&foc));
    out = pid_angle_ctrl(&angle_pid);
    foc_speed_pid_set_target(out);
}

/**
 * @brief 执行 FOC 周期调度。
 * @return int32_t 处理了一个调度 tick 返回 1，否则返回 0。
 * @note TIM2 基准频率为 4000Hz，控制环和传感器采样由运行配置决定。
 */
int32_t foc_app_update(void)
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

    if (foc_update_pending_flag == 0U)
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
        if (AppError_HasActive(APP_ERROR_AS5600_RUNTIME_READ) != 0u)
        {
            foc_current_target = 0;
            foc_set_target(0, 0, 0);
            can_protocol_set_foc_available(0u);
            foc_update_pending_flag = 0U;
            return 1;
        }

        foc_update_speed_estimate();
    }

    if (speed_loop_due != 0U)
    {
        foc_run_speed_loop();
    }

    if ((position_loop_due != 0U)
        && (foc_control_mode == FOC_CTRL_MODE_POSITION))
    {
        foc_run_position_loop();
    }

    if (control_loop_due != 0U)
    {
        foc_control(&foc);
    }

    if (sensor_loop_due != 0U)
    {
        foc_trace_record_sample();
    }

    can_protocol_report_motor_state();

    if (scheduler_tick >= FOC_SCHEDULER_TICK_HZ)
    {
        scheduler_tick = 0U;
    }

    foc_update_pending_flag = 0U;

    return 1;
}

/**
 * @brief 判断当前是否存在待处理的 FOC 调度请求。
 * @return int 存在待处理请求返回 1，否则返回 0。
 */
int foc_app_update_is_pending(void)
{
    if (foc_update_pending_flag != 0U)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 初始化 FOC 运行环境。
 * @return void
 */
void foc_app_init(void)
{
    recoder_data data = {0};

    foc_config_load_saved();
    foc_init(&foc, &foc_runtime_config);
    foc_config_start_pwm();

    if (Load_Recoder(&data) != 0U)
    {
        foc_zero_reset_manual(&foc, data.calibration_angle);
        printf("zero angle loaded: %ld\n", (long)data.calibration_angle);
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
        printf("zero angle saved: %ld\n", (long)data.calibration_angle);
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

/**
 * @brief 定时器周期回调函数。
 * @param htim 定时器句柄指针。
 * @return void
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        foc_update_pending_flag = 1U;
    }
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
    return (int32_t)foc_speed_estimate;
}

/**
 * @brief 获取速度环 PID 运行时诊断数据。
 * @param runtime 运行时诊断数据输出指针。
 * @return void
 */
void foc_speed_pid_get_runtime(foc_speed_pid_runtime_t *runtime)
{
    if (runtime == NULL)
    {
        return;
    }

    runtime->target = speed_pid.target;
    runtime->feedback = speed_pid.percent;
    runtime->error = speed_pid.err;
    runtime->error_delta = speed_pid.err_deta;
    runtime->integral_acc = speed_pid.i_acc;
    runtime->integral_output = speed_pid.i_out;
    runtime->output = speed_pid.out;
    runtime->q_target = foc.target_park.q;
}

/**
 * @brief 启动 FOC 高速环形记录。
 * @return void
 */
void foc_trace_start(void)
{
    foc_trace_write_index = 0U;
    foc_trace_sample_count = 0U;
    foc_trace_post_remaining = 0U;
    foc_trace_state = FOC_TRACE_STATE_ARMED;
}

/**
 * @brief 停止并冻结 FOC 高速记录。
 * @return void
 */
void foc_trace_stop(void)
{
    if ((foc_trace_state == FOC_TRACE_STATE_ARMED)
        || (foc_trace_state == FOC_TRACE_STATE_POST))
    {
        foc_trace_state = FOC_TRACE_STATE_FROZEN;
    }
}

/**
 * @brief 获取 FOC 高速记录状态。
 * @return uint8_t 高速记录状态值。
 */
uint8_t foc_trace_get_state(void)
{
    return foc_trace_state;
}

/**
 * @brief 获取已保存的 FOC 高速采样数量。
 * @return uint16_t 采样数量。
 */
uint16_t foc_trace_get_count(void)
{
    return foc_trace_sample_count;
}

/**
 * @brief 获取 FOC 高速记录采样频率。
 * @return uint16_t 采样频率，单位：Hz。
 */
uint16_t foc_trace_get_sample_hz(void)
{
    return foc.cfg->sensor_hz;
}

/**
 * @brief 按时间顺序读取一条 FOC 高速采样。
 * @param index 按时间排序后的采样下标。
 * @param sample 采样数据输出指针。
 * @return uint8_t 成功返回 1，参数无效返回 0。
 */
uint8_t foc_trace_get_sample(uint16_t index, foc_trace_sample_t *sample)
{
    uint16_t oldest_index = 0U;
    uint16_t physical_index;

    if ((sample == NULL) || (index >= foc_trace_sample_count))
    {
        return 0U;
    }

    if (foc_trace_sample_count >= FOC_TRACE_SAMPLE_COUNT)
    {
        oldest_index = foc_trace_write_index;
    }

    physical_index = (uint16_t)(oldest_index + index);
    if (physical_index >= FOC_TRACE_SAMPLE_COUNT)
    {
        physical_index = (uint16_t)(physical_index - FOC_TRACE_SAMPLE_COUNT);
    }

    *sample = foc_trace_samples[physical_index];
    return 1U;
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
