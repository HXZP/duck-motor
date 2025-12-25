#ifndef __FOC_CORE_H
#define __FOC_CORE_H

//#include "foc/foc_def.h"

#include "math.h"
#include "stdint.h"

/*传感器的值转弧度再x1000传入*/

/*
分析上，从电压矢量分解到相位电压矢量，再分解到Clarke坐标下的静止坐标系，再分解到Park坐标下的旋转坐标系得到d轴和q轴的电压矢量。
电机的力来自磁力，磁力由电流产生，电流由电压产生，电压由电压矢量产生，最后输出的就是能够控制电压矢量的控制时间。

在控制上，控制目标值q轴的电压矢量，再反方向分解到Park坐标下的旋转坐标系，再反方向分解到Clarke坐标下的静止坐标系，再反方向分解到相位电压矢量，最后输出控制时间。
控制电压矢量通过控制七个基本电压矢量的开启时间来实现，并且这六个基本电压矢量将一圈分为六个区，目标矢量在一个区中只需要两个基本矢量就能合成，所以最后只需要控制两个基本矢量的时间。
分区的位置可以通过目标矢量知道

*/
#define FOC_PI (3141/1000)
#define FOC_PIx1000 (3141)
#define SQRT_3 17321/10000  //√3约等于1.7321，放大10000倍防止浮点运算
#define INF_SQRT_3 10000/17321  //√3约等于1.7321，放大10000倍防止浮点运算

#define OUT_MAX_BIT (15)
#define OUT_MAX (1<<OUT_MAX_BIT)


typedef enum{
    
    Foc_Zero_Angle_Init,
    Foc_Working,
    Foc_Shutdown,
}foc_state_e;

typedef struct{

    int32_t a;
    int32_t b;
    int32_t c;
}foc_phase_volage_t;

typedef struct{

    int32_t a;
    int32_t b;
    int32_t c;
}foc_phase_current_t;

typedef struct{

    uint16_t ch1;
    uint16_t ch2;
    uint32_t ch1_sum;
    uint32_t ch2_sum;
    uint8_t cnt;
    uint8_t init_flag;
}foc_current_adc_offset_t;

//-+100*1000表示百分比，放大1000倍防止浮点运算
typedef struct{

    int32_t d;
    int32_t q;
    int32_t theta;
}foc_park_t;

typedef struct{

    int32_t alpha;
    int32_t beta;
}foc_clarke_t;

typedef struct{

    uint8_t A;
    uint8_t B;
    uint8_t C;
    uint8_t N;
    uint8_t value;
}foc_sector_t;

//向量的号码和开启的半桥是对应的ABC对于位为高中低
typedef struct{

    int32_t strength;//0~vector_voltage
    union{
        uint8_t value;//0~7
        struct{

            uint8_t Sc:1;
            uint8_t Sb:1;
            uint8_t Sa:1; 
            uint8_t res:5;       
        }half_bridge;
    }direction;

    int32_t percent;//0~100
}foc_vector_t;

typedef struct{

    int32_t X;
    int32_t Y;
    int32_t Z;
}foc_vector_percent_t;

typedef struct{

    foc_vector_t vector_x;  
    foc_vector_t vector_y;
    foc_vector_t vector_0;
    foc_vector_t vector_7;
}foc_output_vector_t;

//range:0-T
typedef struct{

    uint16_t T;
    uint16_t a;
    uint16_t b;
    uint16_t c;
}foc_pwm_duty_t;

typedef struct{

    //弧度
    int32_t sensor_angle;
    int32_t zero_angle;
    int32_t elec_angle;
    int32_t mech_angle;
	
    int32_t elec_angle360;
    int32_t mech_angle360;	
	int32_t mech_angle360_pre;
	int32_t mech_velocity_rpm;
}foc_angle_t;

typedef struct{

    //弧度
    int32_t mech_angle;
    int32_t last_mech_angle;  
    int32_t mech_angle_diff;  
    
    uint32_t now_time;    
    uint32_t last_time;
    uint32_t time_diff;
    
    int32_t speed;
    
}foc_speed_t;


typedef struct{

    int32_t master_voltage;//母线电压
    int32_t vector_voltage;//基本电压矢量:0~母线电压/sqrt(3)
    uint8_t pole_pairs;//极对数
}foc_mechine_info_t;

typedef struct{
    
    foc_state_e state;
	
    uint8_t pole_pairs;//极对数
    int32_t master_voltage;//母线电压
	
    int32_t zero_angle;
	
    uint16_t pwm_period;
	
	uint16_t pwm_hz;
	uint16_t control_hz;
    uint16_t control_khz;
	uint16_t sensor_hz;
    
	void (*output)(uint16_t a, uint16_t b, uint16_t c);
    void (*delay)(uint32_t ms);
    uint32_t (*get_time)(void); //ms
    int32_t (*get_angle_rad)(void);
}foc_cfg_t;

typedef struct{

    foc_state_e state;
    foc_mechine_info_t info;

    foc_phase_volage_t  phase_volage;
    foc_phase_current_t phase_current;
	
    foc_park_t           park;    
    foc_clarke_t         clarke;
    foc_sector_t         sector;
    foc_output_vector_t  output_vector;
    foc_vector_percent_t vector_percent;
    foc_pwm_duty_t       pwm_duty;

    foc_angle_t  angle;
    foc_speed_t  speed;
    
    foc_park_t         target_park;

    foc_current_adc_offset_t adc_offset;
    
    struct{
    
        uint32_t cnt;
        uint8_t  update_flag;
    }solving;
    
    
	const foc_cfg_t *cfg;
}foc_t;





void foc_init(foc_t *foc, const foc_cfg_t *cfg);
void foc_target_updata(foc_t *foc, foc_park_t target);
int32_t foc_sensor_updata(foc_t *foc);
void foc_control(foc_t *foc);
int32_t foc_zero_reset(foc_t *foc);
void foc_zero_reset_manual(foc_t *foc, int32_t angle);
void foc_adc_offset_get(foc_t *foc, uint16_t* ch1, uint16_t*ch2);
void foc_speed_time_init(foc_t *foc);

uint8_t foc_get_angle_update_flag(foc_t *foc);



#endif /* __FOC_CORE_H */







