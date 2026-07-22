#include "app/hardware_iic.h"

#include "stm32f1xx_hal.h"

#define HARDWARE_I2C_TIMEOUT_MS  2U
#define HARDWARE_I2C_CLOCK_HZ    400000U

/**
 * @brief 等待 I2C1 状态寄存器中的指定标志置位。
 * @param flag 需要等待的 I2C_SR1 标志位。
 * @return uint8_t 成功返回 0，超时或总线错误返回 1。
 */
static uint8_t hardwareI2CWaitSr1Set(uint32_t flag)
{
    uint32_t start_tick = HAL_GetTick();
    uint32_t error_flags = I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_AF | I2C_SR1_OVR;

    while ((I2C1->SR1 & flag) == 0U)
    {
        if ((I2C1->SR1 & error_flags) != 0U)
        {
            return 1U;
        }

        if ((HAL_GetTick() - start_tick) >= HARDWARE_I2C_TIMEOUT_MS)
        {
            return 1U;
        }
    }

    return 0U;
}

/**
 * @brief 等待 I2C1 总线进入空闲状态。
 * @return uint8_t 成功返回 0，超时返回 1。
 */
static uint8_t hardwareI2CWaitBusIdle(void)
{
    uint32_t start_tick = HAL_GetTick();

    while ((I2C1->SR2 & I2C_SR2_BUSY) != 0U)
    {
        if ((HAL_GetTick() - start_tick) >= HARDWARE_I2C_TIMEOUT_MS)
        {
            return 1U;
        }
    }

    return 0U;
}

/**
 * @brief 清除读取地址阶段的 ADDR 标志。
 * @return void
 * @note STM32F1 要求依次读取 SR1 和 SR2 才能清除 ADDR。
 */
static void hardwareI2CClearAddressFlag(void)
{
    I2C1->SR1;
    I2C1->SR2;
    __DSB();
}

/**
 * @brief 根据当前 APB1 时钟配置 I2C1 为 400 kHz 快速模式。
 * @return uint8_t 成功返回 0，时钟参数无效返回 1。
 */
static uint8_t hardwareI2CConfigurePeripheral(void)
{
    uint32_t peripheral_clock_hz = HAL_RCC_GetPCLK1Freq();
    uint32_t peripheral_clock_mhz = peripheral_clock_hz / 1000000U;
    uint32_t clock_control;
    uint32_t rise_time;

    if ((peripheral_clock_mhz < 2U) || (peripheral_clock_mhz > 36U))
    {
        return 1U;
    }

    clock_control = peripheral_clock_hz / (HARDWARE_I2C_CLOCK_HZ * 3U);
    if (clock_control == 0U)
    {
        clock_control = 1U;
    }

    rise_time = ((peripheral_clock_mhz * 3U) / 10U) + 1U;

    I2C1->CR1 = 0U;
    I2C1->CR2 = peripheral_clock_mhz & I2C_CR2_FREQ;
    I2C1->CCR = I2C_CCR_FS | clock_control;
    I2C1->TRISE = rise_time;
    I2C1->CR1 = I2C_CR1_PE | I2C_CR1_ACK;
    return 0U;
}

/**
 * @brief 复位并重新配置 I2C1 外设。
 * @return void
 */
static void hardwareI2CRecoverPeripheral(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;
    __HAL_RCC_I2C1_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C1_RELEASE_RESET();
    hardwareI2CConfigurePeripheral();
}

/**
 * @brief 启动一次 I2C 主机发送并发送从机写地址。
 * @param dev_addr 7 位 I2C 设备地址。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
static uint8_t hardwareI2CStartWrite(uint8_t dev_addr)
{
    I2C1->CR1 |= I2C_CR1_START;
    if (hardwareI2CWaitSr1Set(I2C_SR1_SB) != 0U)
    {
        return 1U;
    }

    I2C1->DR = (uint32_t)(dev_addr << 1);
    if (hardwareI2CWaitSr1Set(I2C_SR1_ADDR) != 0U)
    {
        return 1U;
    }

    hardwareI2CClearAddressFlag();
    return 0U;
}

/**
 * @brief 启动一次 I2C 主机接收并发送从机读地址。
 * @param dev_addr 7 位 I2C 设备地址。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
static uint8_t hardwareI2CStartRead(uint8_t dev_addr)
{
    I2C1->CR1 |= I2C_CR1_START;
    if (hardwareI2CWaitSr1Set(I2C_SR1_SB) != 0U)
    {
        return 1U;
    }

    I2C1->DR = (uint32_t)((dev_addr << 1) | 1U);
    if (hardwareI2CWaitSr1Set(I2C_SR1_ADDR) != 0U)
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief 从当前寄存器指针接收一个或两个字节。
 * @param dev_addr 7 位 I2C 设备地址。
 * @param data 读取数据输出指针。
 * @param len 读取长度，仅支持 1 或 2 字节。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
static uint8_t hardwareI2CReceive(uint8_t dev_addr, uint8_t *data, uint16_t len)
{
    uint32_t interrupt_state;

    if ((data == NULL) || ((len != 1U) && (len != 2U)))
    {
        return 1U;
    }

    I2C1->CR1 &= ~I2C_CR1_POS;
    I2C1->CR1 |= I2C_CR1_ACK;

    if (hardwareI2CStartRead(dev_addr) != 0U)
    {
        hardwareI2CRecoverPeripheral();
        return 1U;
    }

    if (len == 1U)
    {
        I2C1->CR1 &= ~I2C_CR1_ACK;

        interrupt_state = __get_PRIMASK();
        __disable_irq();
        hardwareI2CClearAddressFlag();
        I2C1->CR1 |= I2C_CR1_STOP;
        __set_PRIMASK(interrupt_state);

        if (hardwareI2CWaitSr1Set(I2C_SR1_RXNE) != 0U)
        {
            hardwareI2CRecoverPeripheral();
            return 1U;
        }

        data[0] = (uint8_t)I2C1->DR;
    }
    else
    {
        I2C1->CR1 |= I2C_CR1_POS;

        interrupt_state = __get_PRIMASK();
        __disable_irq();
        hardwareI2CClearAddressFlag();
        I2C1->CR1 &= ~I2C_CR1_ACK;
        __set_PRIMASK(interrupt_state);

        if (hardwareI2CWaitSr1Set(I2C_SR1_BTF) != 0U)
        {
            hardwareI2CRecoverPeripheral();
            return 1U;
        }

        interrupt_state = __get_PRIMASK();
        __disable_irq();
        I2C1->CR1 |= I2C_CR1_STOP;
        data[0] = (uint8_t)I2C1->DR;
        __set_PRIMASK(interrupt_state);

        data[1] = (uint8_t)I2C1->DR;
    }

    I2C1->CR1 &= ~I2C_CR1_POS;
    I2C1->CR1 |= I2C_CR1_ACK;
    return 0U;
}

/**
 * @brief 初始化硬件 I2C1，使用 PB6/PB7 和 400 kHz 时钟。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
uint8_t HardwareI2C_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio_init.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    __HAL_RCC_I2C1_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C1_RELEASE_RESET();
    return hardwareI2CConfigurePeripheral();
}

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
                               uint16_t len)
{
    uint16_t index;

    if ((data == NULL) || (len == 0U))
    {
        return 1U;
    }

    if ((hardwareI2CWaitBusIdle() != 0U) ||
        (hardwareI2CStartWrite(dev_addr) != 0U))
    {
        hardwareI2CRecoverPeripheral();
        return 1U;
    }

    I2C1->DR = reg_addr;
    for (index = 0U; index < len; index++)
    {
        if (hardwareI2CWaitSr1Set(I2C_SR1_TXE) != 0U)
        {
            hardwareI2CRecoverPeripheral();
            return 1U;
        }

        I2C1->DR = data[index];
    }

    if (hardwareI2CWaitSr1Set(I2C_SR1_BTF) != 0U)
    {
        hardwareI2CRecoverPeripheral();
        return 1U;
    }

    I2C1->CR1 |= I2C_CR1_STOP;
    return 0U;
}

/**
 * @brief 从指定设备寄存器读取一个或两个连续字节。
 * @param dev_addr 7 位 I2C 设备地址。
 * @param reg_addr 8 位寄存器地址。
 * @param data 读取数据输出指针。
 * @param len 读取数据长度，仅支持 1 或 2 字节。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
uint8_t HardwareI2C_ReadBytes(uint8_t dev_addr,
                              uint8_t reg_addr,
                              uint8_t *data,
                              uint16_t len)
{
    if ((hardwareI2CWaitBusIdle() != 0U) ||
        (hardwareI2CStartWrite(dev_addr) != 0U))
    {
        hardwareI2CRecoverPeripheral();
        return 1U;
    }

    I2C1->DR = reg_addr;
    if (hardwareI2CWaitSr1Set(I2C_SR1_BTF) != 0U)
    {
        hardwareI2CRecoverPeripheral();
        return 1U;
    }

    return hardwareI2CReceive(dev_addr, data, len);
}

/**
 * @brief 设置指定设备的内部寄存器地址指针。
 * @param dev_addr 7 位 I2C 设备地址。
 * @param reg_addr 8 位寄存器地址。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
uint8_t HardwareI2C_SetReadPointer(uint8_t dev_addr, uint8_t reg_addr)
{
    if ((hardwareI2CWaitBusIdle() != 0U) ||
        (hardwareI2CStartWrite(dev_addr) != 0U))
    {
        hardwareI2CRecoverPeripheral();
        return 1U;
    }

    I2C1->DR = reg_addr;
    if (hardwareI2CWaitSr1Set(I2C_SR1_BTF) != 0U)
    {
        hardwareI2CRecoverPeripheral();
        return 1U;
    }

    I2C1->CR1 |= I2C_CR1_STOP;
    return 0U;
}

/**
 * @brief 从设备当前寄存器地址指针直接读取一个或两个字节。
 * @param dev_addr 7 位 I2C 设备地址。
 * @param data 读取数据输出指针。
 * @param len 读取数据长度，仅支持 1 或 2 字节。
 * @return uint8_t 成功返回 0，失败返回 1。
 */
uint8_t HardwareI2C_ReadCurrentBytes(uint8_t dev_addr,
                                     uint8_t *data,
                                     uint16_t len)
{
    if (hardwareI2CWaitBusIdle() != 0U)
    {
        hardwareI2CRecoverPeripheral();
        return 1U;
    }

    return hardwareI2CReceive(dev_addr, data, len);
}
