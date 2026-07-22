#ifndef FOC_APP_H
#define FOC_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "foc/foc_core.h"

typedef enum
{
    FOC_CTRL_MODE_CURRENT = 0,
    FOC_CTRL_MODE_SPEED = 1,
    FOC_CTRL_MODE_POSITION = 2
} foc_ctrl_mode_t;

/**
 * @brief 速度环 PID 运行时诊断数据。
 */
typedef struct
{
    float target;          /**< 目标速度，单位：mrad/s。 */
    float feedback;        /**< 反馈速度，单位：mrad/s。 */
    float error;           /**< 当前速度误差，单位：mrad/s。 */
    float error_delta;     /**< 相邻控制周期误差变化量，单位：mrad/s。 */
    float integral_acc;    /**< 积分累计值。 */
    float integral_output; /**< 积分输出值。 */
    float output;          /**< PID 最终输出值。 */
    int32_t q_target;      /**< 实际 q 轴目标值。 */
} foc_speed_pid_runtime_t;

/**
 * @brief FOC 高速跟踪采样数据。
 */
typedef struct __attribute__((packed))
{
    uint16_t timestamp_ms;    /**< 系统毫秒时间戳低 16 位。 */
    uint16_t sensor_angle;    /**< 传感器原始角度，单位：mrad。 */
    uint16_t mechanical_angle;/**< 机械角度，单位：mrad。 */
    uint16_t electrical_angle;/**< 归一化电角度，单位：mrad。 */
    int32_t speed;             /**< 估算机械速度，单位：mrad/s。 */
    int16_t q_target;          /**< q 轴目标值。 */
    uint8_t pwm_a;             /**< A 相 PWM 占空比，0~255 对应 0~100%。 */
    uint8_t pwm_b;             /**< B 相 PWM 占空比，0~255 对应 0~100%。 */
    uint8_t pwm_c;             /**< C 相 PWM 占空比，0~255 对应 0~100%。 */
    uint8_t reserved;          /**< 保留字段。 */
} foc_trace_sample_t;

/**
 * @brief 初始化 FOC 控制运行环境。
 * @return void
 */
void foc_app_init(void);

/**
 * @brief 执行一次 FOC 周期调度。
 * @return int32_t 调度执行成功时返回 1，否则返回 0。
 */
int32_t foc_app_update(void);

/**
 * @brief 判断当前是否存在待处理的 FOC 调度请求。
 * @return int 存在待处理请求返回 1，否则返回 0。
 */
int foc_app_update_is_pending(void);

/**
 * @brief 设置 FOC 的 d 轴、q 轴和电角度目标。
 * @param d_target d 轴目标值。
 * @param q_target q 轴目标值。
 * @param theta_target 电角度目标值。
 * @return void
 */
void foc_set_target(int32_t d_target, int32_t q_target, int32_t theta_target);

/**
 * @brief 执行零点重新标定。
 * @return int32_t 标定后的零点角度，单位为 mrad。
 */
int32_t foc_zero_angle_reset(void);

/**
 * @brief 设置电机零点角度。
 * @param angle 零点角度，单位为 mrad。
 * @return void
 */
void foc_set_zero_angle(int32_t angle);

/**
 * @brief 获取当前零点角度。
 * @return int32_t 当前零点角度，单位为 mrad。
 */
int32_t foc_get_zero_angle(void);

/**
 * @brief 设置当前控制模式。
 * @param mode 目标控制模式。
 * @return void
 * @note 切换到电流模式时会立即下发已保存的电流目标。
 */
void foc_control_mode_set(foc_ctrl_mode_t mode);

/**
 * @brief 获取当前控制模式。
 * @return foc_ctrl_mode_t 当前控制模式。
 */
foc_ctrl_mode_t foc_control_mode_get(void);

/**
 * @brief 设置电流模式下的 q 轴目标值。
 * @param target 电流目标值，对应 q 轴内部控制量。
 * @return int32_t 实际生效的目标值。
 * @note 返回值已经过幅值限制，范围为 [-OUT_MAX, OUT_MAX]。
 */
int32_t foc_current_set_target(int32_t target);

/**
 * @brief 设置速度环 PID 参数。
 * @param p 比例系数。
 * @param i 积分系数。
 * @param i_out_max 积分累加上限。
 * @param out_max 输出上限。
 * @return void
 */
void foc_speed_pid_set_param(float p, float i, float i_out_max, float out_max);

/**
 * @brief 设置速度环完整 PID 参数。
 * @param p 比例系数。
 * @param i 积分系数。
 * @param d 微分系数。
 * @param i_acc_max 积分累加上限。
 * @param out_max 输出上限。
 * @return void
 */
void foc_speed_pid_set_full_param(float p, float i, float d, float i_acc_max, float out_max);

/**
 * @brief 设置速度环目标值。
 * @param target 目标速度，单位为 mrad/s。
 * @return void
 */
void foc_speed_pid_set_target(float target);

/**
 * @brief 获取速度环 PID 参数。
 * @param p 比例系数输出指针。
 * @param i 积分系数输出指针。
 * @param i_out_max 积分累加上限输出指针。
 * @param out_max 输出上限输出指针。
 * @return void
 */
void foc_speed_pid_get_param(float *p, float *i, float *i_out_max, float *out_max);

/**
 * @brief 获取速度环完整 PID 参数。
 * @param p 比例系数输出指针。
 * @param i 积分系数输出指针。
 * @param d 微分系数输出指针。
 * @param i_acc_max 积分累加上限输出指针。
 * @param out_max 输出上限输出指针。
 * @return void
 */
void foc_speed_pid_get_full_param(float *p, float *i, float *d, float *i_acc_max, float *out_max);

/**
 * @brief 获取速度环目标值。
 * @param target 目标速度输出指针。
 * @return void
 */
void foc_speed_pid_get_target(float *target);

/**
 * @brief 获取当前速度估算值。
 * @return int32_t 当前速度估算值，单位为 mrad/s。
 */
int32_t foc_get_speed_estimate(void);

/**
 * @brief 获取速度环 PID 运行时诊断数据。
 * @param runtime 运行时诊断数据输出指针。
 * @return void
 */
void foc_speed_pid_get_runtime(foc_speed_pid_runtime_t *runtime);

/**
 * @brief 启动 FOC 高速环形记录。
 * @return void
 */
void foc_trace_start(void);

/**
 * @brief 停止并冻结 FOC 高速记录。
 * @return void
 */
void foc_trace_stop(void);

/**
 * @brief 获取 FOC 高速记录状态。
 * @return uint8_t 状态值，0 表示空闲，1 表示记录，2 表示触发后记录，3 表示已冻结。
 */
uint8_t foc_trace_get_state(void);

/**
 * @brief 获取已保存的 FOC 高速采样数量。
 * @return uint16_t 采样数量。
 */
uint16_t foc_trace_get_count(void);

/**
 * @brief 获取 FOC 高速记录采样频率。
 * @return uint16_t 采样频率，单位：Hz。
 */
uint16_t foc_trace_get_sample_hz(void);

/**
 * @brief 按时间顺序读取一条 FOC 高速采样。
 * @param index 按时间排序后的采样下标。
 * @param sample 采样数据输出指针。
 * @return uint8_t 成功返回 1，参数无效返回 0。
 */
uint8_t foc_trace_get_sample(uint16_t index, foc_trace_sample_t *sample);

/**
 * @brief 设置位置环 PID 参数。
 * @param p 比例系数。
 * @param i 积分系数。
 * @param d 微分系数。
 * @param i_acc_max 积分累加上限。
 * @param out_max 输出上限。
 * @return void
 */
void foc_position_pid_set_param(float p, float i, float d, float i_acc_max, float out_max);

/**
 * @brief 获取位置环 PID 参数。
 * @param p 比例系数输出指针。
 * @param i 积分系数输出指针。
 * @param d 微分系数输出指针。
 * @param i_acc_max 积分累加上限输出指针。
 * @param out_max 输出上限输出指针。
 * @return void
 */
void foc_position_pid_get_param(float *p, float *i, float *d, float *i_acc_max, float *out_max);

/**
 * @brief 设置位置环目标值。
 * @param target 目标位置，单位为 mrad。
 * @return void
 */
void foc_position_pid_set_target(float target);

/**
 * @brief 获取位置环目标值。
 * @param target 目标位置输出指针。
 * @return void
 */
void foc_position_pid_get_target(float *target);


extern foc_t foc;

#ifdef __cplusplus
}
#endif

#endif /* FOC_APP_H */


