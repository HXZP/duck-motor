#include "foc_init.h"
#include "foc/foc_core.h"
#include "as5600.h"

#include "stm32g4xx_hal.h"          // HAL库核心头文件
#include "stm32g4xx_hal_tim.h"      // 定时器HAL库
#include "stm32g4xx_hal_gpio.h"     // GPIO HAL库
#include "stm32g4xx_hal_rcc.h"      // 时钟HAL库
#include <stdio.h>
#include "foc/foc_angle.h"

//extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;

static void GPIO_Init(void);

uint16_t injected_data[2] = {0};

#define CURRENT_WINDOW 350*2
#define CURRENT_DET_DELAY 600+900//实际延时+波动，可能是因为抖动导致了尖峰850

// 4250/3 = 1416 > CURRENT_WINDOW+CURRENT_DET_DELAY=
uint16_t current_window_get(uint16_t pwm_period, uint16_t a, uint16_t b, uint16_t c)
{
    // 使用排序网络对3个元素进行排序（无分支，最高效）
    uint16_t min_val, mid_val, max_val;
    
    // 第一步：比较a和b
    min_val = (a < b) ? a : b;
    max_val = (a > b) ? a : b;
    
    // 第二步：比较c和max_val，确定最大值
    max_val = (c > max_val) ? c : max_val;
    
    // 第三步：比较c和min_val，确定最小值  
    min_val = (c < min_val) ? c : min_val;
    
    // 第四步：中值通过排除法得到
    mid_val = a + b + c - min_val - max_val;
    
    //找到最大窗口
    uint16_t win_1 = min_val;
    uint16_t win_2 = mid_val - min_val;   
    uint16_t win_3 = max_val - mid_val;    
    uint16_t win_4 = pwm_period - max_val;

    uint16_t max_win = win_1;
    uint8_t id = 1;
    uint16_t ccr = 0;
    if(win_2 > max_win){max_win = win_2; id = 2;}
    if(win_3 > max_win){max_win = win_3; id = 3;}
    if(win_4 > max_win){max_win = win_4; id = 4;}
    
    switch(id)
    {
        case 1:
            ccr = 0;
            break;
        
        case 2:
            ccr = min_val;
            break;
        
        case 3:
            ccr = mid_val;
            break;
        
        case 4:
            ccr = max_val;
            break;
    }

    return ccr+CURRENT_WINDOW+CURRENT_DET_DELAY;
}


uint16_t the_max_ccr = 0;
uint16_t aTest = 1000;
uint16_t bTest = 0;
uint16_t cTest = 0;
void foc_output(uint16_t pwm_period, uint16_t a, uint16_t b, uint16_t c)
{
//    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, aTest);
//    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
//    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);
//    
//    the_max_ccr = current_window_get(pwm_period,aTest,0,0);
//    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, the_max_ccr);
    
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, a);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, b);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, c);
    
    the_max_ccr = current_window_get(pwm_period,a,b,c);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, the_max_ccr);
}

foc_t foc;
foc_cfg_t cfg = {

	.pole_pairs = 7,
	.master_voltage = 12,
	.pwm_period = 4250,
	
	.pwm_hz = 20000,
	.control_hz = 1000,
	.sensor_hz = 5000,
    
    .output = foc_output,
    .delay = HAL_Delay,
    .get_angle_rad = as5600GetAngleRadians,
};

AngleEstimator estimator;
AngleEstimatorConfig config =
{
    // 配置参数
    .Ts = 0.0001f,          // 100us (10kHz)
    .window_size = 50,      // 使用50个点进行最小二乘拟合（对应5ms窗口）
    .angle_threshold = M_PI, // π弧度阈值
    .max_angular_vel = 100.0f * M_PI, // 最大100转/秒
    .smoothing_factor = 0.9f, // 平滑因子
};

void foc_root_init(void)
{
    foc_init(&foc,&cfg);

    // 初始化
    AngleEstimator_Init(&estimator, &config);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_ENABLE(&htim1);
    
    GPIO_Init();
    
    HAL_TIM_OC_Start_IT(&htim1, TIM_CHANNEL_4);
    HAL_TIM_Base_Start_IT(&htim1); 
    
//    foc_adc_offset_get(&foc,&injected_data[0],&injected_data[1]);
    
    foc_output_enable(1);
    foc_zero_reset(&foc);

    HAL_TIM_Base_Start_IT(&htim2);    
}

//float foc_get_angle(void)
//{
//    return foc_sensor_updata(&foc);
//}

float foc_get_angle(void)
{
    float raw_angle = foc_sensor_updata(&foc, as5600GetAngleRadians());
    return raw_angle;
}

void foc_set_target(float _d, float _q, float _theta)
{
    if(_d > 100)_d = 100;
    if(_q > 100)_q = 100;
    
    foc_park_t target = {
        .d = foc.info.vector_voltage*_d/100,
        .q = foc.info.vector_voltage*_q/100,
        .theta = _theta,
    };
    foc_target_updata(&foc, target);
}

void foc_output_enable(uint8_t enable)
{
    if(enable)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);    
    }
    else
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);    
    }
}


//参考电压3.3，采样偏置1.5，采样电阻10mo，放大倍数50，4095，(adc/4095*3.3 - 1.5)/50/0.01*1000 = mA
//uint8_t adc_data_get_flag = 0;
//void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef* hadc)
//{
//    if(hadc->Instance == ADC1)
//    {
//        // 获取所有注入通道的数据
//        injected_data[0] = HAL_ADCEx_InjectedGetValue(hadc, ADC_INJECTED_RANK_1);
//        adc_data_get_flag++;
//    }
//    
//    else if(hadc->Instance == ADC2)
//    {
//        // 获取所有注入通道的数据
//        injected_data[1] = HAL_ADCEx_InjectedGetValue(hadc, ADC_INJECTED_RANK_1);
//        adc_data_get_flag++;
//    }
//    
//    if(adc_data_get_flag == 2)
//    {
//        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,1);
//        float ca = ((float)injected_data[0] - foc.adc_offset.ch1)*3.3f*2000/4095;
//        float cb = ((float)injected_data[1] - foc.adc_offset.ch2)*3.3f*2000/4095;
//        float cc = -ca-cb;
//        foc_current_updata(&foc,ca,cb,cc);
//    }
//}

//uint32_t cntget;
//uint32_t ccrget;
//void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
//{
//    if (htim->Instance == TIM1)
//    {
//        // 检查并处理通道4中断
//        if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_4)
//        {
//            cntget = htim->Instance->CNT;
//            ccrget = htim->Instance->CCR4;
//            adc_data_get_flag = 0;
//            HAL_ADCEx_InjectedStart_IT(&hadc1);
//            HAL_ADCEx_InjectedStart_IT(&hadc2);
//            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,0);
//        }
//    }
//}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* 判断是否是TIM2的更新中断 */
  if (htim->Instance == TIM2)
  {
//    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,1);
//    foc_get_angle();  
//    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,0);

//    AngleEstimator_Process(&estimator, foc.angle.sensor_angle);
//    float filtered_angle = AngleEstimator_GetFilteredAngle(&estimator);
//    foc.angle.mech_velocity_rps = AngleEstimator_GetAngularVelocity(&estimator);
//    foc_mech_estimate_updata(&foc, filtered_angle);

    foc_control(&foc);   

    HAL_TIM_IRQHandler(&htim2);
  }
}


//GPIO_PIN_14 Logic high enables OUT. Internalpulldown
//GPIO_PIN_3 Active-low reset input initializesinternal logicanddisablesthe
static void GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
  
  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_14 | GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}



//foc_pid_t speed_pid = {0};

//void foc_pid_speed(void)
//{


//}

////    speed_pid.percent =  foc.angle.sensor_angle; 
////    q_set = foc_pi_ctrl(&speed_pid);  
//      
//    foc_set_target(0,q_set,0);//功率13.7W电流1.14A 

