#ifndef __FOC_INIT_H
#define __FOC_INIT_H

#include <stdint.h>
#include "foc/foc_core.h"

void foc_root_init(void);
int32_t foc_updata(void);
void foc_output_enable(uint8_t enable);
void foc_set_target(int32_t _d, int32_t _q, int32_t _theta);

int32_t foc_zero_angle_reset(void);

// speed pid
void foc_speed_pid_ctrl(void);
void foc_speed_pid_set_param(float p, float i, float i_out_max, float out_max);
void foc_speed_pid_set_target(int32_t target);
void foc_speed_pid_get_param(float *p, float *i, float *i_out_max, float *out_max);
void foc_speed_pid_get_target(int32_t *target);











#endif


