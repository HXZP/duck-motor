#ifndef __FOC_MATH_H
#define __FOC_MATH_H


#include <stdint.h>

typedef struct 
{

	float p;
	float i;
	float d;
	
	float i_acc;
	
	float i_out_max;
	float out_max;

	float i_out;
	float out;
	
	float target;
	float percent;
	float err;
	float err_deta;
}foc_pid_t;



float foc_pi_ctrl(foc_pid_t *pid);







#endif
