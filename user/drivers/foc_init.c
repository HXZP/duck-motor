#include "foc_init.h"
#include "foc/foc_core.h"
#include "as5600.h"

#include "stm32g4xx_hal.h"          // HAL库核心头文件
#include "stm32g4xx_hal_tim.h"      // 定时器HAL库
#include "stm32g4xx_hal_gpio.h"     // GPIO HAL库
#include "stm32g4xx_hal_rcc.h"      // 时钟HAL库
#include <stdio.h>

//extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;

static void GPIO_Init(void);

uint16_t injected_data[2] = {0};

/*当高电平大于一半的时候在高电平采样，低于一半的时候在低电平采样*/
uint16_t the_max_ccr = 0;
void foc_output(uint16_t a, uint16_t b, uint16_t c)
{
//    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1000);
//    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
//    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);
    
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, a);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, b);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, c);
    
    the_max_ccr = a;
    if(the_max_ccr < b)
    {
        the_max_ccr = b;
    }
    if(the_max_ccr < c)
    {
        the_max_ccr = c;
    }
    
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


void foc_root_init(void)
{
    foc_init(&foc,&cfg);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_ENABLE(&htim1);
    
    GPIO_Init();
    
    HAL_TIM_OC_Start_IT(&htim1, TIM_CHANNEL_4);
    HAL_TIM_Base_Start_IT(&htim1);

    //foc_adc_offset_get(&foc, &injected_data[0], &injected_data[1]);    
    
    foc_output_enable(1);
    foc_zero_reset(&foc);

    HAL_TIM_Base_Start_IT(&htim2);    
}

float foc_get_angle(void)
{
    return foc_sensor_updata(&foc);
}

void foc_set_target(uint8_t _d, uint8_t _q, float _theta)
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
uint8_t adc_data_get_flag = 0;
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if(hadc->Instance == ADC1)
    {
        // 获取所有注入通道的数据
        injected_data[0] = HAL_ADCEx_InjectedGetValue(hadc, ADC_INJECTED_RANK_1);
        adc_data_get_flag++;
    }
    
    else if(hadc->Instance == ADC2)
    {
        // 获取所有注入通道的数据
        injected_data[1] = HAL_ADCEx_InjectedGetValue(hadc, ADC_INJECTED_RANK_1);
        adc_data_get_flag++;
    }
    
    if(adc_data_get_flag == 2)
    {
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,1);
    }
}


void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
        // 检查并处理通道4中断
        if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_4)
        {
            adc_data_get_flag = 0;
            HAL_ADCEx_InjectedStart_IT(&hadc1);
            HAL_ADCEx_InjectedStart_IT(&hadc2);
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,0);
        }
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* 判断是否是TIM2的更新中断 */
  if (htim->Instance == TIM2)
  {
//    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,1);
    foc_get_angle();  
    foc_control(&foc);   
//    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,0);
      
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






