#include "app/flash.h"
#include <string.h>

//#define FLASH_PAGE_SIZE    1024      // F103每页1KB
#define FLASH_USER_ADDR    0x0800FC00 // 最后一页起始地址（第63页）
#define DATA_MAGIC         0xA5A5A5A5 // 数据头标志

// 初始化CRC
void CRC_Init(void) {
    __HAL_RCC_CRC_CLK_ENABLE();
}

// 计算数据的CRC32
uint32_t Calculate_CRC32(uint32_t* data, uint32_t len) {
    CRC->CR = CRC_CR_RESET;  // 复位CRC计算器
    for(uint32_t i = 0; i < len; i++) {
        CRC->DR = data[i];
    }
    return CRC->DR;
}

uint8_t LoadCalibrationData(int32_t* angle) {
    MotorCalibrationData data;
    uint32_t* src = (uint32_t*)FLASH_USER_ADDR;
    
    // 1. 读取Flash数据
    memcpy(&data, src, sizeof(MotorCalibrationData));
    
    // 2. 检查数据头标志
    if(data.magic != DATA_MAGIC) {
        return 0; // 数据无效
    }
    
    // 3. 验证CRC32（仅校验magic和angle字段）
    uint32_t computed_crc = Calculate_CRC32((uint32_t*)&data, 2);
    
    if(computed_crc == data.crc32) {
        *angle = data.calibration_angle;
        return 1; // 数据可信
    }
    return 0; // 校验失败
}

void SaveCalibrationData(int32_t angle) {
    MotorCalibrationData data;
    uint32_t PageError = 0;
    
    // 1. 填充数据结构
    data.magic = DATA_MAGIC;
    data.calibration_angle = angle;
    data.crc32 = Calculate_CRC32((uint32_t*)&data, 2); // 计算前8字节的CRC
    
    // 2. 解锁Flash
    HAL_FLASH_Unlock();
    
    // 3. 擦除目标页（必须整页擦除）
    FLASH_EraseInitTypeDef erase;
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = FLASH_USER_ADDR;
    erase.NbPages = 1;
    HAL_FLASHEx_Erase(&erase, &PageError);
    
    // 4. 写入数据（按字32位写入）
    uint32_t* pData = (uint32_t*)&data;
    for(uint32_t i = 0; i < sizeof(data)/4; i++) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, 
                         FLASH_USER_ADDR + i*4, 
                         pData[i]);
    }
    
    // 5. 重新锁住Flash
    HAL_FLASH_Lock();
}







