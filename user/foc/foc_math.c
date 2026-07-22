#include "foc/foc_math.h"
#include "foc/foc_core.h"

#define constrain(data,min,max) (data = data>max?max:(data<min?min:data))

float pid_angle_ctrl(foc_pid_t *pid)
{
    float err = 0;
	err = pid->target - pid->percent;
	
    //这里应该只有角度环需要使用，但目前不影响速度环
    if(err > FOC_PIx1000)
    {
        err -= 2*FOC_PIx1000;
    }
    else if(err < -FOC_PIx1000)
    {
        err += 2*FOC_PIx1000;
    }
    
    pid->err_deta = err - pid->err;
    pid->err = err;
    
	pid->i_acc += pid->err;
    constrain(pid->i_acc, -pid->i_acc_max, pid->i_acc_max);
    
	pid->i_out = pid->i * pid->i_acc;

	pid->out = pid->p * pid->err + pid->i_out + pid->d * pid->err_deta;
	constrain(pid->out, -pid->out_max, pid->out_max);
	
	return pid->out;
}

/**
 * @brief 计算速度环 PID 输出。
 * @param pid 速度环 PID 控制器指针。
 * @return float 速度环输出值。
 * @note 速度误差单位为 mrad/s，不执行角度跨零回绕。
 */
float pid_speed_ctrl(foc_pid_t *pid)
{
    float err = 0.0f;

    err = pid->target - pid->percent;
    pid->err_deta = err - pid->err;
    pid->err = err;

    pid->i_acc += pid->err;
    constrain(pid->i_acc, -pid->i_acc_max, pid->i_acc_max);

    pid->i_out = pid->i * pid->i_acc;
    pid->out = pid->p * pid->err + pid->i_out + pid->d * pid->err_deta;
    constrain(pid->out, -pid->out_max, pid->out_max);

    return pid->out;
}

void pid_set_param(foc_pid_t *pid, float p, float i, float d, float i_acc_max, float out_max)
{
    pid->p = p;
    pid->i = i;
    pid->d = d;
    pid->i_acc_max = i_acc_max;
    pid->out_max = out_max;

    pid->i_acc = 0;
    pid->i_out = 0;
    pid->out = 0;
}

void pid_set_target(foc_pid_t *pid, float target)
{
    pid->target = target;
}

void foc_percent_update(foc_pid_t *pid, float percent)
{
    pid->percent = percent;
}






