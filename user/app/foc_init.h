#ifndef __FOC_INIT_H
#define __FOC_INIT_H

#include <stdint.h>
#include "foc/foc_core.h"

typedef enum
{
    FOC_CTRL_MODE_CURRENT = 0,
    FOC_CTRL_MODE_SPEED = 1,
    FOC_CTRL_MODE_POSITION = 2
} foc_ctrl_mode_t;

/**
 * @brief 初始化 FOC 控制运行环境。
 * @return void
 */
void foc_root_init(void);

/**
 * @brief 执行一次 FOC 周期调度。
 * @return int32_t 调度执行成功时返回 1，否则返回 0。
 */
int32_t foc_updata(void);

/**
 * @brief 使能或关闭功率输出。
 * @param enable 输出使能标志，0 表示关闭，非 0 表示使能。
 * @return void
 */
void foc_output_enable(uint8_t enable);

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
 * @brief 预留的速度环控制接口。
 * @return void
 */
void foc_speed_pid_ctrl(void);

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








#endif


