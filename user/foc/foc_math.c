#include "foc/foc_math.h"
#include "foc/foc_core.h"

#define constrain(data,min,max) (data = data>max?max:(data<min?min:data))


float foc_pid_ctrl(foc_pid_t *pid)
{
    float err = 0;
	err = pid->target - pid->percent;
	
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
	pid->i_out = pid->i * pid->i_acc;
	constrain(pid->i_out, -pid->i_out_max, pid->i_out_max);

	pid->out = pid->p * pid->err + pid->i_out + pid->d * pid->err_deta;
	constrain(pid->out, -pid->out_max, pid->out_max);
	
	return pid->out;
}

void foc_set_pid_param(foc_pid_t *pid, float p, float i, float d, float i_out_max, float out_max)
{
    pid->p = p;
    pid->i = i;
    pid->d = d;
    pid->i_out_max = i_out_max;
    pid->out_max = out_max;

    pid->i_acc = 0;
    pid->i_out = 0;
    pid->out = 0;
}

void foc_set_pid_target(foc_pid_t *pid, float target)
{
    pid->target = target;
}

void foc_percent_update(foc_pid_t *pid, float percent)
{
    pid->percent = percent;
}






