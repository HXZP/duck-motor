#include "foc_init.h"
#include "foc/foc_core.h"
#include "foc/foc_math.h"

#include "as5600.h"
#include "flash.h"
#include "log.h"
#include "can.h"

#include "stm32f1xx_hal.h"          // HAL库核心头文件
#include "stm32f1xx_hal_tim.h"      // 定时器HAL库
#include "stm32f1xx_hal_gpio.h"     // GPIO HAL库
#include "stm32f1xx_hal_rcc.h"      // 时钟HAL库
#include <stdio.h>

//extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;

uint8_t updata_flag = 0;


static void GPIO_Init(void);

void foc_output(uint16_t a, uint16_t b, uint16_t c)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, a);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, b);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, c);
}

foc_t foc;
foc_cfg_t cfg = {

	.pole_pairs = 7,
	.master_voltage = 12000,
	.pwm_period = 1600,
	
	.pwm_hz = 20000,
	.control_hz = 1000,
    .control_khz = 1,
	.sensor_hz = 5000,
    
    .output = foc_output,
    .delay = HAL_Delay,
    .get_angle_rad = as5600GetAngleRadians,
    .get_time = HAL_GetTick,
};

int32_t foc_zero_angle_reset(void)
{
    foc.state = Foc_Zero_Angle_Init;
    return foc_zero_reset(&foc);
}

foc_pid_t angle_pid = {

    .out_max = 100
};

foc_pid_t speed_pid = {0};

int32_t foc_updata(void)
{
    static uint16_t flag_250us = 0;
    if(updata_flag)
    {
        flag_250us++;
        
        //HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,0);

        if(!(flag_250us%2))//500us
        {
            foc_sensor_updata(&foc);
            
        }        
        
        if(!(flag_250us%40))//10ms
        {
            CAN_TxHeaderTypeDef TxHeader;
            
            uint32_t angle = foc_get_angle(&foc);
            uint32_t speed = foc_get_speed(&foc);
            
            uint8_t TxData[3] = {angle&0xFF, (angle>>8)&0xFF, speed}; // 发送的数据
            uint32_t TxMailbox; // 用于返回使用的发送邮箱
            
            // 配置发送报文头
            TxHeader.StdId = 0x110;       // 标准标识符
            TxHeader.ExtId = 0x00;        // 扩展标识符 (标准帧时通常为0)
            TxHeader.IDE = CAN_ID_STD;    // 使用标准帧
            TxHeader.RTR = CAN_RTR_DATA;  // 数据帧
            TxHeader.DLC = 3;             // 数据长度 (0-8字节)  
            HAL_CAN_AddTxMessage(&hcan, &TxHeader, TxData, &TxMailbox);
            
            foc_speed_updata(&foc);
        }
        
        if(!(flag_250us%40))//10ms
        {  
//            float out = 0;
//            foc_percent_update(&angle_pid, foc_get_angle(&foc));
//            out = pid_angle_ctrl(&angle_pid);
//            foc_speed_pid_set_target(out);
        }
        
        int32_t out = 0;
        foc_percent_update(&speed_pid, foc_get_speed(&foc));
        out = pid_speed_ctrl(&speed_pid);
        foc_set_target(0,out,0); 
        
        foc_control(&foc);  

        if(flag_250us == 40*1600)flag_250us = 0;
        
        //HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,1);
        
        updata_flag = 0;
        
        return 1;
    }
    return 0;
}

void foc_root_init(void)
{
    foc_init(&foc,&cfg);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_ENABLE(&htim1);
    
    GPIO_Init();
    
    foc_output_enable(1);

    recoder_data data = {0};
    if(Load_Recoder(&data))
    {
        foc_zero_reset_manual(&foc, data.calibration_angle);
        printf("zero angle loaded: %d\n", data.calibration_angle);
    }
    else
    {
        data.calibration_angle = foc_zero_angle_reset();
        Save_Recoder(data);
        printf("zero angle saved: %d\n", data.calibration_angle);
    }

    HAL_TIM_Base_Start_IT(&htim2);    
}



void foc_set_target(int32_t _d, int32_t _q, int32_t _theta)
{
    if(_d > OUT_MAX)_d = OUT_MAX;
    if(_q > OUT_MAX)_q = OUT_MAX;
    
    foc_park_t target = {
        .d = _d,
        .q = _q,
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

uint16_t led_cnt = 0;
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        
        led_cnt++;

        if(led_cnt <= 700)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,0);
        }
        else if(led_cnt <= 800)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,1);
        }   
        else if(led_cnt <= 900)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,0);
        }
        else if(led_cnt <= 1000)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,1);
        }
        else if(led_cnt <= 1100)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,0);
        }
        else if(led_cnt < 1500)
        {
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,1);
        }
        else if(led_cnt == 1500)
        {
            led_cnt = 0;
        }        
        
         updata_flag = 1;
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

// speed pid



void foc_speed_pid_ctrl(void)
{

    

}

void foc_speed_pid_set_param(float p, float i, float i_out_max, float out_max)
{
    if(i_out_max > OUT_MAX)
        i_out_max = OUT_MAX;
    
    if(out_max > OUT_MAX)
        out_max = OUT_MAX;
    
    pid_set_param(&speed_pid, p, i, 0, i_out_max, out_max);
}

void foc_speed_pid_get_param(float *p, float *i, float *i_out_max, float *out_max)
{
    *p = speed_pid.p;
    *i = speed_pid.i;
    *i_out_max = speed_pid.i_out_max;
    *out_max = speed_pid.out_max;
}

void foc_speed_pid_set_target(float target)
{
    pid_set_target(&speed_pid, target);
}

void foc_speed_pid_get_target(float *target)
{
    *target = speed_pid.target;
}

