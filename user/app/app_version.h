#ifndef APP_VERSION_H
#define APP_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_VERSION_MAJOR    1u       /**< 主版本号，单位：无。 */
#define APP_VERSION_MINOR    0u       /**< 次版本号，单位：无。 */
#define APP_VERSION_PATCH    0u       /**< 修订版本号，单位：无。 */

/**
 * @brief App 版本号。
 */
typedef struct
{
    uint8_t major;      /**< 主版本号，单位：无。 */
    uint8_t minor;      /**< 次版本号，单位：无。 */
    uint8_t patch;      /**< 修订版本号，单位：无。 */
} app_version_t;

/**
 * @brief 获取当前 App 版本号。
 * @return app_version_t 当前 App 版本号。
 */
app_version_t AppVersion_Get(void);

/**
 * @brief 获取当前 App 版本号字符串。
 * @return const char * 当前 App 版本号字符串。
 */
const char *AppVersion_GetString(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_VERSION_H */
