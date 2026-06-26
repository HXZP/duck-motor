#ifndef CHIP_UID_H
#define CHIP_UID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CHIP_UID_WORD_COUNT 3u
#define CHIP_UID_BYTE_COUNT (CHIP_UID_WORD_COUNT * 4u)

/**
 * @brief 芯片唯一 ID 数据。
 */
typedef struct
{
    uint32_t words[CHIP_UID_WORD_COUNT]; /**< 芯片 UID 原始字，单位：无。 */
} chip_uid_t;

/**
 * @brief 读取芯片 96 位唯一 ID。
 * @param uid UID 输出缓冲区。
 * @return void
 */
void ChipUid_Read(chip_uid_t *uid);

/**
 * @brief 获取用于 CAN 管理识别的 32 位短 UID。
 * @return uint32_t 32 位短 UID，单位：无。
 */
uint32_t ChipUid_GetShortId(void);

#ifdef __cplusplus
}
#endif

#endif /* CHIP_UID_H */
