#ifndef __FOC_CORE_H
#define __FOC_CORE_H

#include "foc/foc_def.h"




void foc_init(foc_t *foc, const foc_cfg_t *cfg);
void foc_target_updata(foc_t *foc, foc_park_t target);
float foc_sensor_updata(foc_t *foc,float angle);
void foc_current_updata(foc_t *foc, float a, float b, float c);
void foc_control(foc_t *foc);
void foc_zero_reset(foc_t *foc);
void foc_adc_offset_get(foc_t *foc, uint16_t* ch1, uint16_t*ch2);
float foc_mech_estimate_updata(foc_t *foc, float angle);






#endif /* __FOC_CORE_H */







