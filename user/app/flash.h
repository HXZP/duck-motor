#ifndef __FLASH_H__
#define __FLASH_H__

#include <stdint.h>
#include "stm32f1xx_hal.h"

#define RECODER_DEFAULT_CALIBRATION_ANGLE (0)
#define RECODER_DEFAULT_CAN_ID            (0x10U)

typedef struct {
    int32_t calibration_angle; // 校准角度
    uint32_t can_id; //can的id号
} recoder_data;

typedef struct {
    uint32_t magic;         // 数据头标志
    recoder_data data;
    uint32_t crc32;         // 校验码
} MotorCalibrationData;

uint8_t Load_Recoder(recoder_data* data);
void Save_Recoder(recoder_data data);










#endif


