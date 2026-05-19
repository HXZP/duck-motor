#include "boot/queue.h"

#include <string.h>

/**
 * @brief 初始化 Boot 字节队列。
 * @param queue 队列句柄指针。
 * @param buffer 队列缓冲区。
 * @param buffer_size 队列缓冲区长度，单位：字节。
 * @return void
 */
void BootQueue_Init(BootQueue_t *queue, uint8_t *buffer, uint16_t buffer_size)
{
    if ((queue == NULL) || (buffer == NULL) || (buffer_size == 0u))
    {
        return;
    }

    queue->buffer = buffer;
    queue->buffer_size = buffer_size;
    BootQueue_Clear(queue);
}

/**
 * @brief 清空 Boot 字节队列。
 * @param queue 队列句柄指针。
 * @return void
 */
void BootQueue_Clear(BootQueue_t *queue)
{
    if ((queue == NULL) || (queue->buffer == NULL) || (queue->buffer_size == 0u))
    {
        return;
    }

    queue->head = 0u;
    queue->tail = 0u;
    queue->count = 0u;
    memset(queue->buffer, 0, queue->buffer_size);
}

/**
 * @brief 向 Boot 字节队列写入数据。
 * @param queue 队列句柄指针。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return uint16_t 实际写入长度，单位：字节。
 */
uint16_t BootQueue_Push(BootQueue_t *queue, const uint8_t *data, uint16_t len)
{
    uint16_t written = 0u;

    if ((queue == NULL) || (queue->buffer == NULL) || (data == NULL))
    {
        return 0u;
    }

    while ((written < len) && (queue->count < queue->buffer_size))
    {
        queue->buffer[queue->tail] = data[written];
        queue->tail = (uint16_t)((queue->tail + 1u) % queue->buffer_size);
        queue->count++;
        written++;
    }

    return written;
}

/**
 * @brief 从 Boot 字节队列读取数据。
 * @param queue 队列句柄指针。
 * @param data 输出数据缓冲区。
 * @param len 期望读取长度，单位：字节。
 * @return uint16_t 实际读取长度，单位：字节。
 */
uint16_t BootQueue_Pop(BootQueue_t *queue, uint8_t *data, uint16_t len)
{
    uint16_t read_len = 0u;

    if ((queue == NULL) || (queue->buffer == NULL) || (data == NULL))
    {
        return 0u;
    }

    while ((read_len < len) && (queue->count > 0u))
    {
        data[read_len] = queue->buffer[queue->head];
        queue->head = (uint16_t)((queue->head + 1u) % queue->buffer_size);
        queue->count--;
        read_len++;
    }

    return read_len;
}
