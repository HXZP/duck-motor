#ifndef __FOC_INIT_H
#define __FOC_INIT_H

#include <stdint.h>

void foc_root_init(void);
float foc_get_angle(void);
void foc_output_enable(uint8_t enable);
void foc_set_target(float _d, float _q, float _theta);















#endif


