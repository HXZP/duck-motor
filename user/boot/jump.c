#include "boot/jump.h"

#include "boot/ymodem_process.h"
#include "can.h"
#include "stm32f1xx_hal.h"

#define BOOT_JUMP_SRAM_START_ADDRESS       0x20000000u
#define BOOT_JUMP_SRAM_END_ADDRESS         0x20005000u

typedef void (*boot_jump_entry_fn_t)(void);

/**
 * @brief 读取指定地址处的 32 位字。
 * @param address 目标地址，单位：字节地址。
 * @return uint32_t 地址中的 32 位数据，单位：无。
 */
static uint32_t boot_jump_read_word(uint32_t address)
{
    return *(const uint32_t *)address;
}

/**
 * @brief 检查地址是否落在 SRAM 范围内。
 * @param address 待检查地址，单位：字节地址。
 * @return uint8_t 在 SRAM 范围内返回 1，否则返回 0。
 */
static uint8_t boot_jump_is_sram_address(uint32_t address)
{
    if ((address >= BOOT_JUMP_SRAM_START_ADDRESS) &&
        (address < BOOT_JUMP_SRAM_END_ADDRESS))
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief 检查地址是否落在 App Flash 范围内。
 * @param address 待检查地址，单位：字节地址。
 * @return uint8_t 在 App Flash 范围内返回 1，否则返回 0。
 */
static uint8_t boot_jump_is_app_flash_address(uint32_t address)
{
    uint32_t app_end_address;

    app_end_address = YMODEM_PROCESS_APP_ADDRESS + YMODEM_PROCESS_APP_SIZE;
    if ((address >= YMODEM_PROCESS_APP_ADDRESS) &&
        (address < app_end_address))
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief 检查 App 镜像向量表是否有效。
 * @param app_address App 起始地址，单位：字节地址。
 * @return uint8_t 有效返回 1，无效返回 0。
 */
uint8_t BootJump_IsApplicationValid(uint32_t app_address)
{
    uint32_t stack_pointer;
    uint32_t reset_handler;

    if (app_address != YMODEM_PROCESS_APP_ADDRESS)
    {
        return 0u;
    }

    stack_pointer = boot_jump_read_word(app_address);
    reset_handler = boot_jump_read_word(app_address + 4u);

    if (boot_jump_is_sram_address(stack_pointer) == 0u)
    {
        return 0u;
    }

    if ((reset_handler & 1u) == 0u)
    {
        return 0u;
    }

    if (boot_jump_is_app_flash_address(reset_handler & ~1u) == 0u)
    {
        return 0u;
    }

    return 1u;
}

/**
 * @brief 跳转到 App 入口。
 * @param app_address App 起始地址，单位：字节地址。
 * @return uint8_t 跳转成功不返回，镜像无效时返回 0。
 */
uint8_t BootJump_ToApplication(uint32_t app_address)
{
    uint32_t app_stack;
    uint32_t app_reset;
    boot_jump_entry_fn_t app_entry;

    if (BootJump_IsApplicationValid(app_address) == 0u)
    {
        return 0u;
    }

    app_stack = boot_jump_read_word(app_address);
    app_reset = boot_jump_read_word(app_address + 4u);
    app_entry = (boot_jump_entry_fn_t)app_reset;

    HAL_CAN_Stop(&hcan);
    HAL_DeInit();
    __disable_irq();
    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;
    SCB->VTOR = app_address;
    __set_MSP(app_stack);
    __enable_irq();

    app_entry();
    return 0u;
}
