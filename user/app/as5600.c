#include "as5600.h"
#include "app/hardware_iic.h"
#include "stm32f1xx_hal.h"

#include <stddef.h>

// AS5600 定义
#define AS5600_ADDRESS        0x36 // AS5600设备地址(7位地址)
#define AS5600_RAW_ANGLE_H    0x0C
#define AS5600_RAW_ANGLE_L    0x0D
#define AS5600_ANGLE_H        0x0E
#define AS5600_ANGLE_L        0x0F
#define AS5600_STATUS         0x0B
#define AS5600_CONF_H         0x07
#define AS5600_CONF_L         0x08
#define AS5600_CONF_SF_FAST   0x03
#define AS5600_CONF_FTH_6_LSB 0x04
#define AS5600_CONF_TARGET_H  (AS5600_CONF_SF_FAST | AS5600_CONF_FTH_6_LSB)
#define AS5600_CONF_TARGET_L  0x00U
#define AS5600_CONF_TARGET    (((uint16_t)AS5600_CONF_TARGET_H << 8) | \
                               AS5600_CONF_TARGET_L)

#define PI                    3.14159265359f
#define TWO_PI                6.28318530718f
#define ANGLE_TO_RADIANS      0.00153435538f  // 2π/4096

static uint16_t as5600_boot_config = 0U;
static uint16_t as5600_active_config = 0U;
static uint32_t as5600_read_last_cycles = 0U;
static uint32_t as5600_read_min_cycles = UINT32_MAX;
static uint32_t as5600_read_max_cycles = 0U;
static uint64_t as5600_read_total_cycles = 0U;
static uint32_t as5600_read_count = 0U;

/**
 * @brief 启用 Cortex-M3 DWT 周期计数器。
 * @return void
 */
static void as5600EnableCycleCounter(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
 * @brief 将 CPU 周期数转换为微秒。
 * @param cycles CPU 周期数。
 * @return uint32_t 对应耗时，单位：us。
 */
static uint32_t as5600CyclesToUs(uint64_t cycles)
{
    uint32_t hclk_hz = HAL_RCC_GetHCLKFreq();

    if (hclk_hz == 0U)
    {
        return 0U;
    }

    return (uint32_t)((cycles * 1000000ULL) / hclk_hz);
}


/**
 * @brief 读取AS5600寄存器
 * @param regAddress 寄存器地址
 * @return 读取的数据，0xFF表示读取失败
 */
static uint8_t as5600ReadReg(uint8_t regAddress)
{
    uint8_t data = 0;

    if (HardwareI2C_ReadBytes(AS5600_ADDRESS, regAddress, &data, 1U) == 0U)
    {
        return data;
    }

    return 0xFF;
}

/**
 * @brief 多字节读取AS5600寄存器
 * @param regAddress 起始寄存器地址
 * @param data 读取的数据数组
 * @param len 要读取的数据长度
 * @return 0:成功, 1:失败
 */
static uint8_t as5600ReadMultipleReg(uint8_t regAddress, uint8_t *data, uint8_t len)
{
    return HardwareI2C_ReadBytes(AS5600_ADDRESS, regAddress, data, len);
}

/**
 * @brief 分别读取 AS5600 CONF 高字节和低字节。
 * @param config_high CONF 高字节输出指针。
 * @param config_low CONF 低字节输出指针。
 * @return uint8_t 成功返回 0，失败返回 1。
 * @note CONF 使用两个单字节事务，避免当前器件连续读取低字节时返回 0xFF。
 */
static uint8_t as5600ReadConfig(uint8_t *config_high, uint8_t *config_low)
{
    if ((config_high == NULL) || (config_low == NULL))
    {
        return 1U;
    }

    if (HardwareI2C_ReadBytes(AS5600_ADDRESS,
                              AS5600_CONF_H,
                              config_high,
                              1U) != 0U)
    {
        return 1U;
    }

    if (HardwareI2C_ReadBytes(AS5600_ADDRESS,
                              AS5600_CONF_L,
                              config_low,
                              1U) != 0U)
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief 获取AS5600原始角度值(12位) - 使用多字节读取提高效率
 * @return 原始角度值(0-4095)
 */
uint16_t as5600GetRawAngle(void)
{
    uint8_t angleData[2] = {0U};
    uint8_t read_result;
    uint32_t start_cycles;
    uint32_t elapsed_cycles;

    start_cycles = DWT->CYCCNT;
    read_result = HardwareI2C_ReadBytes(AS5600_ADDRESS,
                                       AS5600_RAW_ANGLE_H,
                                       angleData,
                                       2U);

    if (read_result == 0U)
    {
        elapsed_cycles = DWT->CYCCNT - start_cycles;
        as5600_read_last_cycles = elapsed_cycles;
        as5600_read_total_cycles += elapsed_cycles;
        as5600_read_count++;

        if (elapsed_cycles < as5600_read_min_cycles)
        {
            as5600_read_min_cycles = elapsed_cycles;
        }

        if (elapsed_cycles > as5600_read_max_cycles)
        {
            as5600_read_max_cycles = elapsed_cycles;
        }

        return (angleData[0] & 0x0F) << 8 | angleData[1];
    }

    return 0xFFFF;
}

/**
 * @brief 获取AS5600处理后的角度值(12位) - 使用多字节读取提高效率
 * @return 处理后的角度值(0-4095)
 */
uint16_t as5600GetAngle(void)
{
    uint8_t angleData[2];
    
    // 一次性读取高低字节
    if (as5600ReadMultipleReg(AS5600_ANGLE_H, angleData, 2) == 0) {
        // 组合高4位和低8位
        return (angleData[0] & 0x0F) << 8 | angleData[1];
    }
    
    return 0xFFFF; // 读取失败
}

/**
 * @brief 获取AS5600角度(弧度)
* @return 角度值(0-2π弧度)x1000倍
 */
//uint16_t angle_sensor_same = 0;
//uint16_t angle_get_cnt = 0;
//static uint16_t last_angle = 0;
uint16_t rawAngle4095;
int32_t as5600GetAngleRadians(void)
{
    rawAngle4095 = as5600GetRawAngle();
    
    if (rawAngle4095 == 0xFFFF) return 0xFFFFFFFF; // 读取失败
    return rawAngle4095 * 15343/10000;
}

/**
 * @brief 获取AS5600角度(度)
 * @return 角度值(0-360度)
 */
float as5600GetAngleDegrees(void)
{
    uint16_t rawAngle = as5600GetRawAngle();
    if (rawAngle == 0xFFFF) return -1.0f; // 读取失败
    return (rawAngle * 360.0f) / 4096.0f;
}

/**
 * @brief 获取AS5600状态
 * @param status 状态结构体指针
 * @return 0:成功, 1:失败
 */
uint8_t as5600GetStatus(as5600Status_t *status)
{
    uint8_t statusReg = as5600ReadReg(AS5600_STATUS);
    
    if (statusReg == 0xFF) {
        return 1; // 读取失败
    }
    
    status->magnetTooStrong = (statusReg >> 3) & 0x01;
    status->magnetTooWeak = (statusReg >> 4) & 0x01;
    status->magnetDetected = (statusReg >> 5) & 0x01;
    
    return 0;
}

/**
 * @brief 检查磁铁是否在有效范围内
 * @return 0:磁铁正常, 1:磁铁太强, 2:磁铁太弱, 3:磁铁未检测到, 0xFF:读取失败
 */
uint8_t as5600CheckMagnetStatus(void)
{
    as5600Status_t status;
    
    if (as5600GetStatus(&status)) {
        return 0xFF; // 读取失败
    }
    
    if (status.magnetDetected) {
        if (status.magnetTooStrong) {
            return 1; // 磁铁太强
        } else if (status.magnetTooWeak) {
            return 2; // 磁铁太弱
        } else {
            return 0; // 磁铁正常
        }
    } else {
        return 3; // 磁铁未检测到
    }
}

/**
 * @brief 设置AS5600配置寄存器
 * @param configHigh 高字节配置
 * @param configLow 低字节配置
 * @return 0:成功, 1:失败
 */
uint8_t as5600SetConfig(uint8_t configHigh, uint8_t configLow)
{
    if (HardwareI2C_WriteBytes(AS5600_ADDRESS,
                               AS5600_CONF_H,
                               &configHigh,
                               1U) != 0U)
    {
        return 1U;
    }

    if (HardwareI2C_WriteBytes(AS5600_ADDRESS,
                               AS5600_CONF_L,
                               &configLow,
                               1U) != 0U)
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief 初始化AS5600
 * @return 0:成功, 1:设备无响应, 2:磁铁状态异常
 */
uint8_t as5600Init(void)
{
    uint8_t config_data[2] = {0U};
    uint8_t device_status;
    uint8_t magnet_status;

    as5600EnableCycleCounter();
    device_status = as5600ReadReg(AS5600_STATUS);
    if (device_status == 0xFF)
    {
        return 1;
    }

    if (as5600ReadConfig(&config_data[0], &config_data[1]) != 0U)
    {
        return 1;
    }

    as5600_boot_config = ((uint16_t)config_data[0] << 8) | config_data[1];
    config_data[0] = AS5600_CONF_TARGET_H;
    config_data[1] = AS5600_CONF_TARGET_L;

    if (as5600SetConfig(config_data[0], config_data[1]) != 0U)
    {
        return 1;
    }

    if (as5600ReadConfig(&config_data[0], &config_data[1]) != 0U)
    {
        return 1;
    }

    as5600_active_config = ((uint16_t)config_data[0] << 8) | config_data[1];
    if (as5600_active_config != AS5600_CONF_TARGET)
    {
        return 1;
    }

    magnet_status = as5600CheckMagnetStatus();
    if (magnet_status != 0U)
    {
        return 2;
    }

    if (HardwareI2C_SetReadPointer(AS5600_ADDRESS, AS5600_RAW_ANGLE_H) != 0U)
    {
        return 1;
    }

    as5600_read_last_cycles = 0U;
    as5600_read_min_cycles = UINT32_MAX;
    as5600_read_max_cycles = 0U;
    as5600_read_total_cycles = 0U;
    as5600_read_count = 0U;
    return 0;
}

/**
 * @brief 获取 AS5600 配置和读取耗时诊断数据。
 * @param diagnostic 诊断数据输出指针。
 * @return void
 */
void as5600GetDiagnostic(as5600_diagnostic_t *diagnostic)
{
    uint64_t average_cycles = 0U;

    if (diagnostic == NULL)
    {
        return;
    }

    if (as5600_read_count > 0U)
    {
        average_cycles = as5600_read_total_cycles / as5600_read_count;
    }

    diagnostic->boot_config = as5600_boot_config;
    diagnostic->active_config = as5600_active_config;
    diagnostic->read_last_us = as5600CyclesToUs(as5600_read_last_cycles);
    if (as5600_read_min_cycles == UINT32_MAX)
    {
        diagnostic->read_min_us = 0U;
    }
    else
    {
        diagnostic->read_min_us = as5600CyclesToUs(as5600_read_min_cycles);
    }

    diagnostic->read_max_us = as5600CyclesToUs(as5600_read_max_cycles);
    diagnostic->read_average_us = as5600CyclesToUs(average_cycles);
}

/**
 * @brief 获取AS5600设备信息
 * @param chipVersion 芯片版本号指针
 * @return 0:成功, 1:失败
 */
uint8_t as5600GetDeviceInfo(uint8_t *chipVersion)
{
    // 读取芯片版本寄存器(0x01)
    uint8_t version = as5600ReadReg(0x01);
    if (version == 0xFF) {
        return 1;
    }
    
    *chipVersion = version;
    return 0;
}

///**
// * @brief 高效连续读取多个角度值
// * @param angles 角度数据数组
// * @param count 要读取的数量
// * @return 实际读取的数量
// */
//uint8_t as5600ReadMultipleAngles(uint16_t *angles, uint8_t count)
//{
//    uint8_t successCount = 0;
//    
//    for (uint8_t i = 0; i < count; i++) {
//        uint16_t angle = as5600GetRawAngle();
//        if (angle != 0xFFFF) { // 有效角度检查
//            angles[i] = angle;
//            successCount++;
//        }
//    }
//    
//    return successCount;
//}

///**
// * @brief 批量读取原始角度数据（最高效方式）
// * @param angleData 角度数据数组
// * @param count 要读取的角度数量
// * @return 0:成功, 1:失败
// */
//uint8_t as5600BulkReadRawAngles(uint16_t *angleData, uint8_t count)
//{
//    if (count == 0) return 0;
//    
//    // 一次性读取所有角度数据
//    uint8_t *dataBuffer = (uint8_t *)malloc(count * 2);
//    if (dataBuffer == NULL) return 1;
//    
//    // 从RAW_ANGLE_H开始连续读取
//    if (as5600ReadMultipleReg(AS5600_RAW_ANGLE_H, dataBuffer, count * 2) != 0) {
//        free(dataBuffer);
//        return 1;
//    }
//    
//    // 处理数据
//    for (uint8_t i = 0; i < count; i++) {
//        angleData[i] = (dataBuffer[i * 2] & 0x0F) << 8 | dataBuffer[i * 2 + 1];
//    }
//    
//    free(dataBuffer);
//    return 0;
//}

// 使用示例
//void as5600Example(void)
//{
//    // 初始化AS5600
//    uint8_t initResult = as5600Init();
//    
//    switch (initResult) {
//        case 0:
//            printf("AS5600 initialized successfully.\n");
//            break;
//        case 1:
//            printf("AS5600 device not responding.\n");
//            return;
//        case 2:
//            printf("AS5600 magnet status abnormal.\n");
//            break;
//        default:
//            printf("AS5600 unknown error.\n");
//            return;
//    }
//    
//    // 读取角度值
//    uint16_t rawAngle = as5600GetRawAngle();
//    uint16_t processedAngle = as5600GetAngle();
//    float angleRad = as5600GetAngleRadians();
//    float angleDeg = as5600GetAngleDegrees();
//    
//    printf("Raw Angle: %d\n", rawAngle);
//    printf("Processed Angle: %d\n", processedAngle);
//    printf("Angle (Rad): %.3f\n", angleRad);
//    printf("Angle (Deg): %.1f\n", angleDeg);
//    
//    // 检查磁铁状态
//    uint8_t magnetStatus = as5600CheckMagnetStatus();
//    switch (magnetStatus) {
//        case 0:
//            printf("Magnet: Normal\n");
//            break;
//        case 1:
//            printf("Magnet: Too Strong\n");
//            break;
//        case 2:
//            printf("Magnet: Too Weak\n");
//            break;
//        case 3:
//            printf("Magnet: Not Detected\n");
//            break;
//        default:
//            printf("Magnet: Status Error\n");
//            break;
//    }
//    
//    // 批量读取示例
//    uint16_t angles[10];
//    if (as5600BulkReadRawAngles(angles, 10) == 0) {
//        printf("Bulk read successful:\n");
//        for (int i = 0; i < 10; i++) {
//            printf("Angle[%d]: %d\n", i, angles[i]);
//        }
//    }
//}
