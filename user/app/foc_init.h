#ifndef __FOC_INIT_H
#define __FOC_INIT_H

#include <stdint.h>

void foc_root_init(void);
int32_t foc_get_angle(void);
void foc_output_enable(uint8_t enable);
void foc_set_target(int32_t _d, int32_t _q, int32_t _theta);















#endif


