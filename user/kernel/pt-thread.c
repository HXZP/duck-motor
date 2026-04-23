#include "pt-thread.h"

#include "stm32f1xx_hal.h"

typedef struct
{
    Thread_t pt;
    Thread_Func_t func;
    unsigned char active;
} Thread_Slot_t;

static Thread_Slot_t s_thread_pool[THREAD_MAX];

/**
 * @brief 按槽位索引删除一个线程任务。
 * @param slot_index 待删除的槽位索引。
 * @return int 删除成功返回 1，失败返回 0。
 */
static int thread_delete_by_index(int slot_index)
{
    if ((slot_index < 0) || (slot_index >= THREAD_MAX))
    {
        return 0;
    }

    if (s_thread_pool[slot_index].active == 0U)
    {
        return 0;
    }

    s_thread_pool[slot_index].active = 0U;
    s_thread_pool[slot_index].func = NULL;
    return 1;
}

/**
 * @brief 删除一个协作式线程任务。
 * @param func 待删除的任务函数。
 * @return int 删除成功返回 1，失败返回 0。
 */
int Thread_Delete(Thread_Func_t func)
{
    int index = 0;

    if (func == NULL)
    {
        return 0;
    }

    for (index = 0; index < THREAD_MAX; index++)
    {
        if ((s_thread_pool[index].active != 0U) &&
            (s_thread_pool[index].func == func))
        {
            return thread_delete_by_index(index);
        }
    }

    return 0;
}

/**
 * @brief 创建一个协作式线程任务。
 * @param func 待注册的任务函数。
 * @return int 创建成功返回槽位索引，失败返回 -1。
 */
int Thread_Create(Thread_Func_t func)
{
    int index = 0;

    if (func == NULL)
    {
        return -1;
    }

    for (index = 0; index < THREAD_MAX; index++)
    {
        if ((s_thread_pool[index].active != 0U) &&
            (s_thread_pool[index].func == func))
        {
            return index;
        }
    }

    for (index = 0; index < THREAD_MAX; index++)
    {
        if (s_thread_pool[index].active == 0U)
        {
            PT_INIT(&s_thread_pool[index].pt);
            s_thread_pool[index].func = func;
            s_thread_pool[index].active = 1U;
            return index;
        }
    }

    return -1;
}

/**
 * @brief 获取当前系统毫秒节拍。
 * @return unsigned int 当前毫秒节拍值。
 */
unsigned int Thread_GetTick(void)
{
    return (unsigned int)HAL_GetTick();
}

/**
 * @brief 调度当前所有活跃的协作式线程任务。
 * @return void
 */
void Thread_Schedule(void)
{
    int index = 0;

    for (index = 0; index < THREAD_MAX; index++)
    {
        if ((s_thread_pool[index].active != 0U) &&
            (s_thread_pool[index].func != NULL))
        {
            char result = s_thread_pool[index].func(&s_thread_pool[index].pt);

            if (result >= PT_EXITED)
            {
                thread_delete_by_index(index);
            }
        }
    }
}
