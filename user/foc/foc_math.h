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
    float i_acc_max;
	float i_out_max;
	float out_max;	

	float err;
	float err_deta;    
    
	float i_acc;
    
	float i_out;
	float out;

}foc_pid_t;

float pid_angle_ctrl(foc_pid_t *pid);
float pid_speed_ctrl(foc_pid_t *pid);

void pid_set_param(foc_pid_t *pid, float p, float i, float d, float i_out_max, float out_max);
void pid_set_target(foc_pid_t *pid, float target);
void foc_percent_update(foc_pid_t *pid, float percent);







#endif
