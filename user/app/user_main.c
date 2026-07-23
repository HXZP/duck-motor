#include "app/user_main.h"

#include "main.h"
#include "app/app_error.h"
#include "app/as5600.h"
#include "app/app_light.h"
#include "app/can_protocol.h"
#include "app/foc_app.h"
#include "app/foc_config.h"
#include "app/hardware_iic.h"
#include "app/log.h"
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
        THREAD_WAIT_UNTIL(foc_app_update_is_pending() != 0);
        foc_app_update();
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
 * @param foc_available FOC 可用标志，0 表示不可用，非 0 表示可用。
 * @return void
 */
static void user_main_create_tasks(uint8_t foc_available)
{
    if (foc_available != 0U)
    {
        if (Thread_Create(user_main_foc_thread) < 0)
        {
            printf("create user_main_foc_thread failed\r\n");
            AppError_Set(APP_ERROR_FOC_THREAD_CREATE);
            can_protocol_set_foc_available(0U);
            foc_config_prepare_safe_output();
            AppLight_SetMode(APP_LIGHT_MODE_ERROR);
        }
    }

    if (Thread_Create(user_main_can_thread) < 0)
    {
        printf("create user_main_can_thread failed\r\n");
        AppError_Set(APP_ERROR_CAN_THREAD_CREATE);
        AppLight_SetMode(APP_LIGHT_MODE_ERROR);
    }
}

/**
 * @brief 初始化用户业务并运行主调度循环。
 * @return void
 */
void User_Main(void)
{
    uint8_t foc_available = 1U;

    if (s_user_main_started != 0U)
    {
        return;
    }

    s_user_main_started = 1U;

    __enable_irq();
    log_init();
    printf("App start\r\n");
    AppError_Init();
    AppLight_Init();

    if (HardwareI2C_Init() != 0U)
    {
        printf("Hardware I2C init failed\r\n");
        AppError_Set(APP_ERROR_HARDWARE_I2C_INIT);
        foc_available = 0U;
    }
    else
    {
        if (as5600Init() != 0U)
        {
            printf("AS5600 init failed: errors=0x%08lX\r\n",
                   (unsigned long)AppError_GetActive());
            foc_available = 0U;
        }
    }

    if (foc_available != 0U)
    {
        foc_app_init();
    }
    else
    {
        foc_config_prepare_safe_output();
        AppLight_SetMode(APP_LIGHT_MODE_ERROR);
    }

    can_protocol_init(foc_available);
    user_main_create_tasks(foc_available);
    printf("App init done\r\n");

    while (1)
    {
        AppLight_Poll();
        can_protocol_poll();
        Thread_Schedule();
        __WFI();
    }
}
