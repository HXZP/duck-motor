#ifndef HARDWARE_IIC_H
#define HARDWARE_IIC_H

#include <stdint.h>

/**
 * @brief 初始化硬件 I2C1，使用 PB6/PB7 和 400kHz 时钟。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
uint8_t HardwareI2C_Init(void);

/**
 * @brief 向指定设备寄存器写入连续数据。
 * @param dev_addr 7 位 I2C 设备地址。
 * @param reg_addr 8 位寄存器地址。
 * @param data 待写入数据指针。
 * @param len 写入数据长度，单位：字节。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
uint8_t HardwareI2C_WriteBytes(uint8_t dev_addr,
                               uint8_t reg_addr,
                               const uint8_t *data,
                               uint16_t len);

/**
 * @brief 从指定设备寄存器读取连续数据。
 * @param dev_addr 7 位 I2C 设备地址。
 * @param reg_addr 8 位寄存器地址。
 * @param data 读取数据输出指针。
 * @param len 读取数据长度，单位：字节。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
uint8_t HardwareI2C_ReadBytes(uint8_t dev_addr,
                              uint8_t reg_addr,
                              uint8_t *data,
                              uint16_t len);

/**
 * @brief 设置指定设备的内部寄存器地址指针。
 * @param dev_addr 7 位 I2C 设备地址。
 * @param reg_addr 8 位寄存器地址。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
uint8_t HardwareI2C_SetReadPointer(uint8_t dev_addr, uint8_t reg_addr);

/**
 * @brief 从设备当前寄存器地址指针直接读取数据。
 * @param dev_addr 7 位 I2C 设备地址。
 * @param data 读取数据输出指针。
 * @param len 读取数据长度，单位：字节。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
uint8_t HardwareI2C_ReadCurrentBytes(uint8_t dev_addr,
                                     uint8_t *data,
                                     uint16_t len);

#endif
