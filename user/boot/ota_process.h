#ifndef OTA_PROCESS_H
#define OTA_PROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 初始化 Boot OTA 处理模块。
 * @return void
 */
void OtaProcess_Init(void);

/**
 * @brief 轮询推进 Boot OTA 状态机。
 * @return void
 */
void OtaProcess_Poll(void);

/**
 * @brief 标记收到 OTA 启动请求。
 * @return void
 */
void OtaProcess_SetRequestPending(void);

/**
 * @brief 查询当前是否处于 YMODEM 数据接收阶段。
 * @return uint8_t 非 0 表示允许接收 YMODEM 数据，0 表示未进入传输阶段。
 */
uint8_t OtaProcess_IsTransferEnabled(void);

/**
 * @brief 向 OTA 接收缓冲区压入一段字节流。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int OtaProcess_PushRxBytes(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* OTA_PROCESS_H */
