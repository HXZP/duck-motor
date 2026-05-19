#ifndef BOOT_QUEUE_H
#define BOOT_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 字节环形队列控制结构。
 */
typedef struct
{
    uint8_t *buffer;       /**< 队列缓冲区。 */
    uint16_t buffer_size;  /**< 队列容量，单位：字节。 */
    uint16_t head;         /**< 读索引，单位：字节。 */
    uint16_t tail;         /**< 写索引，单位：字节。 */
    uint16_t count;        /**< 当前数据量，单位：字节。 */
} BootQueue_t;

/**
 * @brief 初始化字节环形队列。
 * @param queue 队列控制结构。
 * @param buffer 外部分配的队列缓冲区。
 * @param buffer_size 队列缓冲区大小，单位：字节。
 * @return void
 */
void BootQueue_Init(BootQueue_t *queue, uint8_t *buffer, uint16_t buffer_size);

/**
 * @brief 清空字节环形队列。
 * @param queue 队列控制结构。
 * @return void
 */
void BootQueue_Clear(BootQueue_t *queue);

/**
 * @brief 向队列写入一段字节数据。
 * @param queue 队列控制结构。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return uint16_t 实际写入长度，单位：字节。
 */
uint16_t BootQueue_Push(BootQueue_t *queue, const uint8_t *data, uint16_t len);

/**
 * @brief 从队列读取一段字节数据。
 * @param queue 队列控制结构。
 * @param data 输出数据缓冲区。
 * @param len 期望读取长度，单位：字节。
 * @return uint16_t 实际读取长度，单位：字节。
 */
uint16_t BootQueue_Pop(BootQueue_t *queue, uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_QUEUE_H */
