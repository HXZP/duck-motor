/*
    基于SVPWM的FOC算法实现

*/
#include "foc/foc_core.h"
#include <stdlib.h>
#include "arm_math.h"
/*
    **  @brief  N值到扇区
*/
const uint8_t N2sector[6] = {2,6,1,4,3,5};
/*
    **  @brief  扇区到两个子向量 顺序为扇区的逆时针方向
*/
const uint8_t sector2vector[6][2] = {
    {4, 6},
    {6, 2},
    {2, 3},
    {3, 1},
    {1, 5},
	{5, 4}
};
/*
    **  @brief  从扇区到两个基向量的加权 顺序为扇区的逆时针方向
*/
//常见xy的向量方向：由小序号指向大序号
//const int8_t sector2vector_mask[6][2][3] = {
//    {{0,0,-1},{1,0,0}},
//    {{0,0,1},{0,1,0}},
//    {{1,0,0},{0,-1,0}},
//    {{-1,0,0},{0,0,1}},
//    {{0,-1,0},{0,0,-1}},
//    {{0,1,0},{-1,0,0}}
//};
//我的向量方向，按逆时针指向
const int8_t sector2vector_mask[6][2][3] = {
    {{0,0,-1},{1,0,0}},
    {{0,1,0},{0,0,1}},
    {{1,0,0},{0,-1,0}},
    {{0,0,1},{-1,0,0}},
    {{0,-1,0},{0,0,-1}},
    {{-1,0,0},{0,1,0}}
};
/*
    **  @brief  基本模型
*/
/*
    **  @brief  Clarke变换
                a-b-c和α-β的关系
*/
void foc_clarke(foc_clarke_t *clarke, const foc_phase_volage_t *phase_volage)
{
    clarke->alpha = (phase_volage->a - phase_volage->b - phase_volage->c)*2/3;
    clarke->beta = (phase_volage->b - phase_volage->c)/SQRT_3;
}
/*
    **  @brief  Park变换
                α-β和d-q的关系
*/
void foc_park(foc_park_t *park, const foc_clarke_t *clarke)
{
    park->d = clarke->alpha * arm_cos_f32(park->theta) + clarke->beta * arm_sin_f32(park->theta);
    park->q =-clarke->alpha * arm_sin_f32(park->theta) + clarke->beta * arm_cos_f32(park->theta);
}
/*
    **  @brief  反Clarke变换
                α-β和a-b-c的关系
*/
void foc_reclarke(foc_phase_volage_t *phase_volage, const foc_clarke_t *clarke)
{
    phase_volage->a = clarke->alpha;
    phase_volage->b = -clarke->alpha/2 + clarke->beta * SQRT_3/2;
    phase_volage->c = -clarke->alpha/2 - clarke->beta * SQRT_3/2;
}
/*
    **  @brief  反Park变换
                d-q和α-β的关系
*/
void foc_repark(foc_clarke_t *clarke, const foc_park_t *park)
{
    clarke->alpha = park->d * arm_cos_f32(park->theta) - park->q * arm_sin_f32(park->theta);
    clarke->beta = park->d * arm_sin_f32(park->theta) + park->q * arm_cos_f32(park->theta);
}

/*
    **  @brief  扇区判断,根据向量落点对于三根直线的关系获取扇区,本质上的是判断向量落在哪个象限
*/
void foc_get_sector(foc_sector_t *sector, const foc_clarke_t *clarke)
{
	sector->A = (clarke->beta > 0)?1:0;
	sector->B = (SQRT_3 * clarke->alpha - clarke->beta > 0)?1:0;
	sector->C = (-SQRT_3 * clarke->alpha - clarke->beta > 0)?1:0;
	
    sector->N = 4 * sector->C + 2 * sector->B + sector->A;
    sector->value = N2sector[sector->N - 1];
}

void foc_get_vector_percent(foc_vector_percent_t *vector_percent, const float vector_voltage, const foc_clarke_t *clarke)
{
    vector_percent->X = clarke->beta/vector_voltage * 100;
    vector_percent->Y = (clarke->alpha * SQRT_3/2 + clarke->beta/2) /vector_voltage * 100;
    vector_percent->Z = (-clarke->alpha * SQRT_3/2 + clarke->beta/2) /vector_voltage * 100;
}

void foc_get_vector_normalize(foc_output_vector_t *vector, const foc_vector_percent_t *vector_percent, const foc_sector_t *sector)
{
    float percent_x = 0;
    float percent_y = 0;

    percent_x = sector2vector_mask[sector->value - 1][0][0] * vector_percent->X
                + sector2vector_mask[sector->value - 1][0][1] * vector_percent->Y
                  + sector2vector_mask[sector->value - 1][0][2] * vector_percent->Z;

    percent_y = sector2vector_mask[sector->value - 1][1][0] * vector_percent->X
                + sector2vector_mask[sector->value - 1][1][1] * vector_percent->Y
                  + sector2vector_mask[sector->value - 1][1][2] * vector_percent->Z;

    if(percent_x + percent_y > 100)
    {
        vector->vector_x.percent = percent_x/(percent_x + percent_y) * 100;
        vector->vector_y.percent = percent_y/(percent_x + percent_y) * 100;
    }                
    else
    {
        vector->vector_x.percent = percent_x;
        vector->vector_y.percent = percent_y;
    }
		
    if(percent_x + percent_y == 0)
    {
        vector->vector_0.percent = 0;
        vector->vector_7.percent = 0;		
    }
    else
    {
        vector->vector_0.percent = (100 - percent_x - percent_y)/2;
        vector->vector_7.percent = (100 - percent_x - percent_y)/2;		
    }

    vector->vector_x.direction.value = sector2vector[sector->value - 1][0];
    vector->vector_y.direction.value = sector2vector[sector->value - 1][1];
    vector->vector_0.direction.value = 0;
    vector->vector_7.direction.value = 7;
}

void foc_get_vector_strength(foc_output_vector_t *vector, const float vector_voltage)
{
    vector->vector_x.strength = vector->vector_x.percent * vector_voltage/100;
    vector->vector_y.strength = vector->vector_y.percent * vector_voltage/100;
    vector->vector_0.strength = vector->vector_0.percent * vector_voltage/100;
    vector->vector_7.strength = vector->vector_7.percent * vector_voltage/100;
}

void foc_get_pwm_duty(foc_pwm_duty_t *pwm, const foc_output_vector_t *vector)
{
    float pwm_x = 0;
    float pwm_y = 0;
    float pwm_0 = 0;	
    float pwm_7 = 0;		
    
    pwm_x = vector->vector_x.percent * pwm->T/100;
    pwm_y = vector->vector_y.percent * pwm->T/100;
    pwm_0 = vector->vector_0.percent * pwm->T/100;
    pwm_7 = vector->vector_7.percent * pwm->T/100;

	pwm->a = pwm_x * vector->vector_x.direction.half_bridge.Sa
            + pwm_y * vector->vector_y.direction.half_bridge.Sa
            + pwm_0 * vector->vector_0.direction.half_bridge.Sa
            + pwm_7 * vector->vector_7.direction.half_bridge.Sa;

	pwm->b = pwm_x * vector->vector_x.direction.half_bridge.Sb
            + pwm_y * vector->vector_y.direction.half_bridge.Sb
            + pwm_0 * vector->vector_0.direction.half_bridge.Sb
            + pwm_7 * vector->vector_7.direction.half_bridge.Sb;     
	
    pwm->c = pwm_x * vector->vector_x.direction.half_bridge.Sc
            + pwm_y * vector->vector_y.direction.half_bridge.Sc
            + pwm_0 * vector->vector_0.direction.half_bridge.Sc
            + pwm_7 * vector->vector_7.direction.half_bridge.Sc;        

}


float foc_angle_cycle(float angle)
{
    if(angle > 2 * FOC_PI)
    {
        return angle - 2 * FOC_PI;
    }
    else if(angle < 0)
    {
        return angle + 2 * FOC_PI;
    }
    else
    {
        return angle;
    }
}


void foc_init(foc_t *foc, const foc_cfg_t *cfg)
{
    foc->info.master_voltage = cfg->master_voltage;
    foc->info.vector_voltage = foc->info.master_voltage/SQRT_3;
	
    foc->info.pole_pairs = cfg->pole_pairs;
    foc->pwm_duty.T = cfg->pwm_period;
	
	foc->cfg = cfg;
}

/*angle:0~1*/
float foc_sensor_updata(foc_t *foc,float angle)
{  
    //float angle = foc->cfg->get_angle_rad();
    
    foc->angle.sensor_angle = angle;
    foc->angle.mech_angle = foc_angle_cycle(foc->angle.sensor_angle - foc->angle.zero_angle);
    foc->angle.elec_angle = foc->angle.mech_angle * foc->info.pole_pairs;
	
	foc->park.theta = foc->angle.elec_angle;
	
//	foc->angle.mech_angle360_pre = foc->angle.mech_angle360;
//	foc->angle.elec_angle360 = foc->angle.elec_angle / 2 /FOC_PI * 360;
//	foc->angle.mech_angle360 = foc->angle.mech_angle / 2 /FOC_PI * 360;
//	foc->angle.mech_velocity_rpm = (foc->angle.mech_angle360 - foc->angle.mech_angle360_pre) * foc->cfg->sensor_hz/60;// deta角度/deta时间 = 角速度（°/s） 角速度/360*60 = 转速rpm

    return angle;
}

float foc_mech_estimate_updata(foc_t *foc, float angle)
{
    //float angle = foc->cfg->get_angle_rad();

    foc->angle.mech_angle = foc_angle_cycle(angle - foc->angle.zero_angle);
    foc->angle.elec_angle = foc->angle.mech_angle * foc->info.pole_pairs;
    
    foc->park.theta = foc->angle.elec_angle;
    
    return foc->angle.mech_angle;
}

void foc_target_updata(foc_t *foc, foc_park_t target)
{
    // if(target->d * target->d + target->q * target->q > foc->info.vector_voltage * foc->info.vector_voltage)
    // {
    //     // 如果目标电压的平方和大于最大电压的平方和，则将目标电压限制在最大电压范围内
    // }

    foc->target_park = target;


}

void foc_current_updata(foc_t *foc, float a, float b, float c)
{
//    static float a
    
    if(foc->state != Foc_Working)
    {
        return;
    }
    
    foc->phase_current.a = a;
    foc->phase_current.b = b;
    foc->phase_current.c = c;
}

void foc_control(foc_t *foc)
{
    if(foc->state != Foc_Working)
    {
        return;
    }
    
    foc->park.d = foc->target_park.d;
    foc->park.q = foc->target_park.q;

    foc_repark(&foc->clarke, &foc->park);
	
    foc_get_sector(&foc->sector, &foc->clarke);
    foc_get_vector_percent(&foc->vector_percent, foc->info.vector_voltage, &foc->clarke);
    foc_get_vector_normalize(&foc->output_vector, &foc->vector_percent, &foc->sector);
//    foc_get_vector_strength(&foc->output_vector, foc->info.vector_voltage);
    foc_get_pwm_duty(&foc->pwm_duty, &foc->output_vector);
    
    foc->cfg->output(foc->cfg->pwm_period,foc->pwm_duty.a,foc->pwm_duty.b,foc->pwm_duty.c);
}

void foc_zero_reset(foc_t *foc)
{
    if(foc->state == Foc_Zero_Angle_Init)
    {
        foc->cfg->output(foc->cfg->pwm_period,foc->cfg->pwm_period*0.5f,0,0);
        foc->cfg->delay(200);
        foc->angle.sensor_angle = foc->cfg->get_angle_rad();
        foc->angle.zero_angle = foc->angle.sensor_angle;
        foc->cfg->output(foc->cfg->pwm_period,0,0,0);
        foc->state = Foc_Working;
    }
}

void foc_adc_offset_get(foc_t *foc, uint16_t* ch1, uint16_t*ch2)
{
//    uint16_t *adc_data_ch1 = malloc(sizeof(uint16_t)*250);
//    uint16_t *adc_data_ch2 = malloc(sizeof(uint16_t)*250);

    uint16_t adc_data_ch1[250] = {0};
    uint16_t adc_data_ch2[250] = {0};
    
    for(foc->adc_offset.cnt = 0; foc->adc_offset.cnt < 250; foc->adc_offset.cnt++)
    {
        adc_data_ch1[foc->adc_offset.cnt] = *ch1;
        adc_data_ch2[foc->adc_offset.cnt] = *ch2;
        foc->cfg->delay(1);
    }

    for(foc->adc_offset.cnt = 0; foc->adc_offset.cnt < 250; foc->adc_offset.cnt++)
    {
        foc->adc_offset.ch1_sum += adc_data_ch1[foc->adc_offset.cnt];
        foc->adc_offset.ch2_sum += adc_data_ch2[foc->adc_offset.cnt];
    }

    foc->adc_offset.ch1 = foc->adc_offset.ch1_sum / 250;
    foc->adc_offset.ch2 = foc->adc_offset.ch2_sum / 250;

    foc->adc_offset.init_flag = 1;
}
    
    


