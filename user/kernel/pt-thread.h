#ifndef PT_THREAD_H
#define PT_THREAD_H

#include "pt.h"
#include "pt-sem.h"

#define THREAD_MAX      30

typedef struct pt Thread_t;
typedef struct pt_sem Thread_Sem_t;

typedef char (*Thread_Func_t)(Thread_t *pt);

/**
 * @brief 创建一个协作式线程任务。
 * @param func 待注册的任务函数。
 * @return int 创建成功返回槽位索引，失败返回 -1。
 */
int Thread_Create(Thread_Func_t func);

/**
 * @brief 删除一个协作式线程任务。
 * @param func 待删除的任务函数。
 * @return int 删除成功返回 1，失败返回 0。
 */
int Thread_Delete(Thread_Func_t func);

/**
 * @brief 调度当前所有活跃的协作式线程任务。
 * @return void
 */
void Thread_Schedule(void);

/**
 * @brief 获取当前系统毫秒节拍。
 * @return unsigned int 当前毫秒节拍值。
 */
unsigned int Thread_GetTick(void);

#define THREAD_DEF(name) char name(Thread_t *_pt_)

#define THREAD_BEGIN() PT_BEGIN(_pt_)

#define THREAD_END() PT_END(_pt_)

#define THREAD_WAIT_UNTIL(cond) PT_WAIT_UNTIL(_pt_, cond)

#define THREAD_WAIT_WHILE(cond) PT_WAIT_WHILE(_pt_, cond)

#define THREAD_SLEEP(ms) \
    do \
    { \
        static unsigned int _thread_sleep_end_; \
        _thread_sleep_end_ = Thread_GetTick() + (ms); \
        PT_WAIT_UNTIL(_pt_, (int)(Thread_GetTick() - _thread_sleep_end_) >= 0); \
    } while (0)

#define THREAD_YIELD() PT_YIELD(_pt_)

#define THREAD_YIELD_UNTIL(cond) PT_YIELD_UNTIL(_pt_, cond)

#define THREAD_RESTART() PT_RESTART(_pt_)

#define THREAD_EXIT() PT_EXIT(_pt_)

#define THREAD_WAIT_CHILD(child, child_call) PT_SPAWN(_pt_, &(child), child_call)

#define THREAD_SEM_INIT(sem, count) PT_SEM_INIT(&(sem), count)

#define THREAD_SEM_WAIT(sem) PT_SEM_WAIT(_pt_, &(sem))

#define THREAD_SEM_SIGNAL(sem) (++(sem).count)

#endif
