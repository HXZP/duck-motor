#ifndef YMODEM_PROCESS_H
#define YMODEM_PROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "boot/ymodem_core.h"
#include "common/user_info.h"

#define YMODEM_PROCESS_APP_ADDRESS              0x08004400u
#define YMODEM_PROCESS_USER_INFO_ADDRESS        USER_INFO_FLASH_ADDRESS
#define YMODEM_PROCESS_APP_SIZE                 (YMODEM_PROCESS_USER_INFO_ADDRESS - YMODEM_PROCESS_APP_ADDRESS)
#define YMODEM_PROCESS_FLASH_PAGE_SIZE          1024u
#define YMODEM_PROCESS_SIZE_FIRST_HEAD_PACKET   128u
#define YMODEM_PROCESS_HEAD_PACKET_INFO_LENGTH  128u

/**
 * @brief YMODEM 文件信息。
 */
typedef struct
{
    uint8_t file_name[YMODEM_CORE_FILE_NAME_LENGTH];              /**< 文件名。 */
    uint8_t head_packet_info[YMODEM_PROCESS_HEAD_PACKET_INFO_LENGTH]; /**< 首包业务头信息。 */
    int32_t file_size;                                            /**< 文件总长度，单位：字节。 */
    uint32_t payload_size;                                        /**< 实际写入长度，单位：字节。 */
} ymodem_process_file_info_t;

/**
 * @brief 初始化 YMODEM 处理模块。
 * @return void
 */
void YmodemProcess_Init(void);

/**
 * @brief 重置当前 YMODEM 会话。
 * @return void
 */
void YmodemProcess_ResetSession(void);

/**
 * @brief 启动一次新的 YMODEM 接收会话。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemProcess_StartSession(void);

/**
 * @brief 轮询推进 YMODEM 接收会话。
 * @return int 成功结束返回 BOOT_OK，进行中返回 BOOT_ERR_BUSY，失败返回 BOOT_ERR_xxx。
 */
int YmodemProcess_PollSession(void);

/**
 * @brief 查询当前是否处于 YMODEM 传输阶段。
 * @return uint8_t 非 0 表示传输中，0 表示未传输。
 */
uint8_t YmodemProcess_IsTransferEnabled(void);

/**
 * @brief 向 YMODEM 接收缓冲区压入一段字节流。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemProcess_PushRxBytes(const uint8_t *data, uint16_t len);

/**
 * @brief 获取当前会话解析出的文件信息。
 * @param file_info 文件信息输出缓冲区。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemProcess_GetFileInfo(ymodem_process_file_info_t *file_info);

#ifdef __cplusplus
}
#endif

#endif /* YMODEM_PROCESS_H */
