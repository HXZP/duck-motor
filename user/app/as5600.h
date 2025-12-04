#ifndef __AS5600_DRV_H
#define __AS5600_DRV_H

#include <stdint.h>

// AS5600 状态标志
typedef struct {
    uint8_t magnetTooStrong : 1;
    uint8_t magnetTooWeak : 1;
    uint8_t magnetDetected : 1;
} as5600Status_t;


uint8_t as5600Init(void);
uint8_t as5600GetDeviceInfo(uint8_t *chipVersion);
uint16_t as5600GetRawAngle(void);
uint16_t as5600GetAngle(void);
int32_t as5600GetAngleRadians(void);
float as5600GetAngleDegrees(void);
uint8_t as5600GetStatus(as5600Status_t *status);
uint8_t as5600CheckMagnetStatus(void);
uint8_t as5600SetConfig(uint8_t configHigh, uint8_t configLow);



#endif

