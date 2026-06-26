#include "common/chip_uid.h"

#include <stddef.h>

#define CHIP_UID_BASE_ADDRESS          0x1FFFF7E8u   /**< STM32F103 唯一 ID 起始地址，单位：地址。 */
#define CHIP_UID_FNV1A_OFFSET_BASIS    2166136261u   /**< FNV-1a 初始哈希值，单位：无。 */
#define CHIP_UID_FNV1A_PRIME           16777619u     /**< FNV-1a 乘数，单位：无。 */

/**
 * @brief 向 FNV-1a 哈希中追加字节。
 * @param hash 当前哈希值，单位：无。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return uint32_t 追加后的哈希值，单位：无。
 */
static uint32_t chip_uid_hash_update(uint32_t hash, const uint8_t *data, uint32_t len)
{
    uint32_t index;

    if ((data == NULL) && (len != 0u))
    {
        return hash;
    }

    for (index = 0u; index < len; index++)
    {
        hash = hash ^ data[index];
        hash = hash * CHIP_UID_FNV1A_PRIME;
    }

    return hash;
}

/**
 * @brief 读取芯片 96 位唯一 ID。
 * @param uid UID 输出缓冲区。
 * @return void
 */
void ChipUid_Read(chip_uid_t *uid)
{
    uint32_t index;
    const volatile uint32_t *uid_words = (const volatile uint32_t *)CHIP_UID_BASE_ADDRESS;

    if (uid == NULL)
    {
        return;
    }

    for (index = 0u; index < CHIP_UID_WORD_COUNT; index++)
    {
        uid->words[index] = uid_words[index];
    }
}

/**
 * @brief 获取用于 CAN 管理识别的 32 位短 UID。
 * @return uint32_t 32 位短 UID，单位：无。
 */
uint32_t ChipUid_GetShortId(void)
{
    chip_uid_t uid;
    uint32_t hash;

    ChipUid_Read(&uid);
    hash = chip_uid_hash_update(CHIP_UID_FNV1A_OFFSET_BASIS,
                                (const uint8_t *)uid.words,
                                CHIP_UID_BYTE_COUNT);

    if (hash == 0u)
    {
        hash = CHIP_UID_FNV1A_OFFSET_BASIS;
    }

    return hash;
}
