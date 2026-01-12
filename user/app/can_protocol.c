#include "stm32f1xx_it.h"
#include "can.h"
#include "log.h"
#include "flash.h"
#include "app/foc_init.h"

void CAN_protocol_analysis(CAN_RxHeaderTypeDef rxframe, uint8_t *RxData)
{
    switch(rxframe.StdId)
    {
        case 0x100:
        {
            if(RxData[0])
            {
                foc_set_state(&foc, Foc_Shutdown);
            }
            else
            {
                foc_set_state(&foc, Foc_Working);
            }
            break;        
        }
        
        case 0x105:
        {
            foc_speed_pid_set_param(
            ((float)((RxData[1]<<8)|RxData[0]))/1000, 
            ((float)((RxData[3]<<8)|RxData[2]))/1000,  
            ((RxData[5]<<8)|RxData[4]), 
            ((RxData[7]<<8)|RxData[6])
            );
            break;        
        }
        
        case 0x106:
        {
            foc_speed_pid_set_target((int32_t)RxData[0]);
            break;        
        }
    }
}

/* 重写 HAL_CAN_RxFifo0MsgPendingCallback，处理 FIFO0 的数据 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef RxHeader;
    uint8_t RxData[8];
    
    // 注意：回调函数内仍需调用 GetRxMessage 来读取并释放FIFO
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK)
    {
        printf("CAN ID: 0x%08lX, DLC: %d, Data: ", RxHeader.StdId, RxHeader.DLC);
        for (int i = 0; i < RxHeader.DLC; i++)
        {
            printf("%02X ", RxData[i]);
        }
        printf("\r\n");
        // 在这里处理接收到的数据 RxData
        // 可以通过 RxHeader.StdId 或 RxHeader.ExtId 来区分不同报文
        // 这里就是你的业务逻辑处理位置，与之前写在ISR里一样
    }
}