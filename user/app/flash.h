#ifndef __FLASH_H__
#define __FLASH_H__

#include <stdint.h>
#include "stm32f1xx_hal.h"

typedef struct {
    uint32_t magic;         // 数据头标志
    int32_t calibration_angle; // 校准角度
    uint32_t crc32;         // 校验码
} MotorCalibrationData;

uint8_t LoadCalibrationData(int32_t* angle);
void SaveCalibrationData(int32_t angle);










#endif


