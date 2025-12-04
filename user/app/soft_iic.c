#include "soft_iic.h"


// I2C延时函数（基于68MHz系统时钟）
static void I2C_Delay(void)
{
    volatile uint32_t delay = 10;  // 调整这个值来改变时序
    while(delay--);
}

// 初始化I2C GPIO
void I2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // 使能GPIO时钟
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // 配置SCL和SDA为开漏输出
    GPIO_InitStruct.Pin = I2C_SCL_PIN | I2C_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(I2C_SDA_PORT, &GPIO_InitStruct);
    
    // 初始状态：SCL和SDA都置高
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
}

// 产生起始信号
static void I2C_Start(void)
{
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    I2C_Delay();
    
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
    I2C_Delay();
    
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
    I2C_Delay();
}

// 产生停止信号
static void I2C_Stop(void)
{
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    I2C_Delay();
    
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
    I2C_Delay();
}

// 发送应答信号
static void I2C_Ack(void)
{
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    I2C_Delay();
    
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
    I2C_Delay();
    
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
}

// 发送非应答信号
static void I2C_NAck(void)
{
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    I2C_Delay();
    
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
    I2C_Delay();
}

// 等待应答
uint16_t iic_time_out_cnt = 0;
static uint8_t I2C_Wait_Ack(void)
{
    uint8_t ack;
    
    // 切换SDA为输入模式
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = I2C_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(I2C_SDA_PORT, &GPIO_InitStruct);
    
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
    I2C_Delay();
    
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
//    I2C_Delay();
    
    uint16_t wait_cnt = 1000;
    // 读取应答信号
    while(wait_cnt)
    {
        if(HAL_GPIO_ReadPin(I2C_SDA_PORT, I2C_SDA_PIN) == GPIO_PIN_RESET)
        {
            ack = 0;  // 收到应答     
            break;
        }
        
        wait_cnt--;
        if(!wait_cnt)
        {
            ack = 1;  // 未收到应答    
            iic_time_out_cnt++;    
        }
        
    }
    
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
    I2C_Delay();
    
    // 切换SDA回输出模式
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(I2C_SDA_PORT, &GPIO_InitStruct);
    
    
    HAL_GPIO_TogglePin(I2C_SDA_PORT, I2C_SDA_PIN);
    return ack;
}

// 发送一个字节
static void I2C_Send_Byte(uint8_t data)
{
    uint8_t i;
    
    for(i = 0; i < 8; i++)
    {
        if(data & 0x80)
            HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
        
        data <<= 1;
        I2C_Delay();
        
        HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
        I2C_Delay();
        
        HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
        I2C_Delay();
    }
}

// 接收一个字节
static uint8_t I2C_Read_Byte(uint8_t ack)
{
    uint8_t i, data = 0;
    
    // 切换SDA为输入模式
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = I2C_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(I2C_SDA_PORT, &GPIO_InitStruct);
    
    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
    
    for(i = 0; i < 8; i++)
    {
        data <<= 1;
        
        HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
        I2C_Delay();
        
        if(HAL_GPIO_ReadPin(I2C_SDA_PORT, I2C_SDA_PIN) == GPIO_PIN_SET)
            data |= 0x01;
        
        HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
        I2C_Delay();
    }
    
    // 切换SDA回输出模式
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(I2C_SDA_PORT, &GPIO_InitStruct);
    
    // 发送应答/非应答
    if(ack)
        I2C_Ack();
    else
        I2C_NAck();
    
    return data;
}

// 多字节写入函数
uint8_t I2C_Write_Bytes(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    I2C_Start();
    
    I2C_Send_Byte(dev_addr << 1);  // 写地址
    if(I2C_Wait_Ack())
    {
        I2C_Stop();
        return 1;  // 错误
    }
    
    I2C_Send_Byte(reg_addr);       // 寄存器地址
    if(I2C_Wait_Ack())
    {
        I2C_Stop();
        return 1;  // 错误
    }
    
    // 发送多个数据字节
    for(uint16_t i = 0; i < len; i++)
    {
        I2C_Send_Byte(data[i]);
        if(I2C_Wait_Ack())
        {
            I2C_Stop();
            return 1;  // 错误
        }
    }
    
    I2C_Stop();
    return 0;  // 成功
}

// 多字节读取函数
uint8_t I2C_Read_Bytes(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    I2C_Start();
    
    I2C_Send_Byte(dev_addr << 1);  // 写地址
    if(I2C_Wait_Ack())
    {
        I2C_Stop();
        return 1;  // 错误
    }
    
    I2C_Send_Byte(reg_addr);       // 寄存器地址
    if(I2C_Wait_Ack())
    {
        I2C_Stop();
        return 1;  // 错误
    }
    
    I2C_Start();
    
    I2C_Send_Byte((dev_addr << 1) | 0x01);  // 读地址
    if(I2C_Wait_Ack())
    {
        I2C_Stop();
        return 1;  // 错误
    }
    
    // 读取多个数据字节
    for(uint16_t i = 0; i < len; i++)
    {
        if(i == len - 1)
        {
            // 最后一个字节发送非应答
            data[i] = I2C_Read_Byte(0);
        }
        else
        {
            // 非最后一个字节发送应答
            data[i] = I2C_Read_Byte(1);
        }
    }
    
    I2C_Stop();
    return 0;  // 成功
}



