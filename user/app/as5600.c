#include "as5600.h"
#include "soft_iic.h"

// AS5600 定义
#define AS5600_ADDRESS        0x36 // AS5600设备地址(7位地址)
#define AS5600_RAW_ANGLE_H    0x0C
#define AS5600_RAW_ANGLE_L    0x0D
#define AS5600_ANGLE_H        0x0E
#define AS5600_ANGLE_L        0x0F
#define AS5600_STATUS         0x0B
#define AS5600_CONF_H         0x07
#define AS5600_CONF_L         0x08

#define PI                    3.14159265359f
#define TWO_PI                6.28318530718f
#define ANGLE_TO_RADIANS      0.00153435538f  // 2π/4096


/**
 * @brief 写入AS5600寄存器
 * @param regAddress 寄存器地址
 * @param data 要写入的数据
 * @return 0:成功, 1:失败
 */
static uint8_t as5600WriteReg(uint8_t regAddress, uint8_t data)
{
    return I2C_Write_Bytes(AS5600_ADDRESS, regAddress, &data, 1);
}

/**
 * @brief 多字节写入AS5600寄存器
 * @param regAddress 起始寄存器地址
 * @param data 要写入的数据数组
 * @param len 数据长度
 * @return 0:成功, 1:失败
 */
static uint8_t as5600WriteMultipleReg(uint8_t regAddress, uint8_t *data, uint8_t len)
{
    return I2C_Write_Bytes(AS5600_ADDRESS, regAddress, data, len);
}

/**
 * @brief 读取AS5600寄存器
 * @param regAddress 寄存器地址
 * @return 读取的数据，0xFF表示读取失败
 */
static uint8_t as5600ReadReg(uint8_t regAddress)
{
    uint8_t data = 0;
    if (I2C_Write_Bytes(AS5600_ADDRESS, regAddress, &data, 1) == 0) {
        return data;
    }
    return 0xFF; // 读取失败
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
    return I2C_Read_Bytes(AS5600_ADDRESS, regAddress, data, len);
}

/**
 * @brief 获取AS5600原始角度值(12位) - 使用多字节读取提高效率
 * @return 原始角度值(0-4095)
 */
uint16_t as5600GetRawAngle(void)
{
    uint8_t angleData[2];
    
    // 一次性读取高低字节
    if (as5600ReadMultipleReg(AS5600_RAW_ANGLE_H, angleData, 2) == 0) {
        // 组合高4位和低8位
        return (angleData[0] & 0x0F) << 8 | angleData[1];
    }
    
    return 0xFFFF; // 读取失败
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
    uint8_t configData[2] = {configHigh, configLow};
    return as5600WriteMultipleReg(AS5600_CONF_H, configData, 2);
}

/**
 * @brief 初始化AS5600
 * @return 0:成功, 1:设备无响应, 2:磁铁状态异常
 */
uint8_t as5600Init(void)
{
    // 检查设备是否响应
    uint8_t deviceStatus = as5600ReadReg(AS5600_STATUS);
    if (deviceStatus == 0xFF) {
        return 1; // 设备无响应
    }
    
    // 检查磁铁状态
    uint8_t magnetStatus = as5600CheckMagnetStatus();
    if (magnetStatus != 0) {
        return 2; // 磁铁状态异常
    }
    
    return 0; // 初始化成功
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