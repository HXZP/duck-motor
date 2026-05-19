#include "boot/crc16.h"

#include <stddef.h>

/**
 * @brief 计算 CRC16-CCITT 校验值。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return uint16_t CRC16 校验值，单位：无。
 */
uint16_t BootCrc16_CcittCalc(const uint8_t *data, uint32_t len)
{
    uint32_t index;
    uint16_t crc = 0u;

    if ((data == NULL) && (len != 0u))
    {
        return 0u;
    }

    for (index = 0u; index < len; index++)
    {
        uint8_t bit;

        crc = (uint16_t)(crc ^ ((uint16_t)data[index] << 8));
        for (bit = 0u; bit < 8u; bit++)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1u) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1u);
            }
        }
    }

    return crc;
}
