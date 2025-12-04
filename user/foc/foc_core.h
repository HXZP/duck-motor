#ifndef __FOC_CORE_H
#define __FOC_CORE_H

#include "foc/foc_def.h"




void foc_init(foc_t *foc, const foc_cfg_t *cfg);
void foc_target_updata(foc_t *foc, foc_park_t target);
int32_t foc_sensor_updata(foc_t *foc);
void foc_control(foc_t *foc);
int32_t foc_zero_reset(foc_t *foc);
void foc_zero_reset_manual(foc_t *foc, int32_t angle);
void foc_adc_offset_get(foc_t *foc, uint16_t* ch1, uint16_t*ch2);
void foc_speed_time_init(foc_t *foc);





#endif /* __FOC_CORE_H */







