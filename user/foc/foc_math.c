#include "foc/foc_math.h"

#define constrain(data,min,max) (data = data>max?max:(data<min?min:data))


float foc_pi_ctrl(foc_pid_t *pid)
{
	pid->err = pid->target - pid->percent;
	
	pid->i_acc += pid->err;
	pid->i_out = pid->i * pid->i_acc;
	
	constrain(pid->i_out, -pid->i_out, pid->i_out);
	constrain(pid->out, -pid->out_max, pid->out_max);

	pid->out = pid->p * pid->err + pid->i_out;
	
	return pid->out;
}












