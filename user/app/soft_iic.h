#ifndef __IIC_DRV_H
#define __IIC_DRV_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

// 软件I2C引脚定义
#define I2C_SCL_PIN    GPIO_PIN_7
#define I2C_SCL_PORT   GPIOB
#define I2C_SDA_PIN    GPIO_PIN_6
#define I2C_SDA_PORT   GPIOB

void I2C_Init(void);
uint8_t I2C_Write_Bytes(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);
uint8_t I2C_Read_Bytes(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

#endif

