#include "app/user_main.h"

#include "main.h"
#include "app/as5600.h"
#include "app/can_protocol.h"
#include "app/foc_init.h"
#include "app/log.h"
#include "app/soft_iic.h"
#include "kernel/pt-thread.h"

static uint8_t s_user_main_started = 0U;

/**
 * @brief 电机 FOC 调度线程。
 * @param _pt_ protothread 控制块指针。
 * @return char protothread 运行状态。
 */
static THREAD_DEF(user_main_foc_thread)
{
    THREAD_BEGIN();

    while (1)
    {
        THREAD_WAIT_UNTIL(foc_update_is_pending() != 0);
        foc_updata();
    }

    THREAD_END();
}

/**
 * @brief CAN 协议处理线程。
 * @param _pt_ protothread 控制块指针。
 * @return char protothread 运行状态。
 */
static THREAD_DEF(user_main_can_thread)
{
    THREAD_BEGIN();

    while (1)
    {
        THREAD_WAIT_UNTIL(can_protocol_has_pending() != 0);
        can_protocol_process();
    }

    THREAD_END();
}

/**
 * @brief 创建用户业务线程。
 * @return void
 */
static void user_main_create_tasks(void)
{
    if (Thread_Create(user_main_foc_thread) < 0)
    {
        printf("create user_main_foc_thread failed\r\n");
        Error_Handler();
        return;
    }

    if (Thread_Create(user_main_can_thread) < 0)
    {
        printf("create user_main_can_thread failed\r\n");
        Error_Handler();
        return;
    }
}

/**
 * @brief 初始化用户业务并运行主调度循环。
 * @return void
 */
void User_Main(void)
{
    if (s_user_main_started != 0U)
    {
        return;
    }

    s_user_main_started = 1U;

    log_init();
    can_protocol_init();
    I2C_Init();
    as5600Init();
    foc_root_init();
    user_main_create_tasks();

    while (1)
    {
        Thread_Schedule();
        __WFI();
    }
}
