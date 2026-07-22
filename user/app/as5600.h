#ifndef __AS5600_DRV_H
#define __AS5600_DRV_H

#include <stdint.h>

// AS5600 状态标志
typedef struct {
    uint8_t magnetTooStrong : 1;
    uint8_t magnetTooWeak : 1;
    uint8_t magnetDetected : 1;
} as5600Status_t;

/**
 * @brief AS5600 配置和读取耗时诊断数据。
 */
typedef struct
{
    uint16_t boot_config;       /**< 启动时读取的 CONF 寄存器值。 */
    uint16_t active_config;     /**< 当前生效的 CONF 寄存器值。 */
    uint32_t read_last_us;      /**< 最近一次角度读取耗时，单位：us。 */
    uint32_t read_min_us;       /**< 最小角度读取耗时，单位：us。 */
    uint32_t read_max_us;       /**< 最大角度读取耗时，单位：us。 */
    uint32_t read_average_us;   /**< 平均角度读取耗时，单位：us。 */
} as5600_diagnostic_t;


uint8_t as5600Init(void);
uint8_t as5600GetDeviceInfo(uint8_t *chipVersion);
uint16_t as5600GetRawAngle(void);
uint16_t as5600GetAngle(void);
int32_t as5600GetAngleRadians(void);
float as5600GetAngleDegrees(void);
uint8_t as5600GetStatus(as5600Status_t *status);
uint8_t as5600CheckMagnetStatus(void);
uint8_t as5600SetConfig(uint8_t configHigh, uint8_t configLow);

/**
 * @brief 获取 AS5600 配置和读取耗时诊断数据。
 * @param diagnostic 诊断数据输出指针。
 * @return void
 */
void as5600GetDiagnostic(as5600_diagnostic_t *diagnostic);



#endif

