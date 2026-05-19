#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 检查 App 镜像向量表是否有效。
 * @param app_address App 起始地址，单位：字节地址。
 * @return uint8_t 有效返回 1，无效返回 0。
 */
uint8_t BootJump_IsApplicationValid(uint32_t app_address);

/**
 * @brief 跳转到 App 入口。
 * @param app_address App 起始地址，单位：字节地址。
 * @return uint8_t 跳转成功不返回，镜像无效时返回 0。
 */
uint8_t BootJump_ToApplication(uint32_t app_address);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_JUMP_H */
