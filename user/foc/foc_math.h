#ifndef __FOC_MATH_H
#define __FOC_MATH_H


#include <stdint.h>

typedef struct 
{
	float target;
	float percent; 
        
	float p;
	float i;
	float d;
	float i_out_max;
	float out_max;	

	float err;
	float err_deta;    
    
	float i_acc;
	float i_out;
	float out;

}foc_pid_t;

float foc_pid_ctrl(foc_pid_t *pid);
void foc_set_pid_param(foc_pid_t *pid, float p, float i, float d, float i_out_max, float out_max);
void foc_set_pid_target(foc_pid_t *pid, float target);
void foc_percent_update(foc_pid_t *pid, float percent);







#endif
