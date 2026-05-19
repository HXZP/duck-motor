#ifndef BOOT_CRC16_H
#define BOOT_CRC16_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 计算 CRC16-CCITT 校验值。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return uint16_t CRC16-CCITT 校验值，单位：无。
 */
uint16_t BootCrc16_CcittCalc(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_CRC16_H */
