#ifndef APP_VERSION_H
#define APP_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_VERSION_MAJOR    1        /**< 主版本号，单位：无。 */
#define APP_VERSION_MINOR    0        /**< 次版本号，单位：无。 */
#define APP_VERSION_PATCH    28       /**< 修订版本号，单位：无。 */

#define APP_VERSION_STRING_VALUE(value)    #value
#define APP_VERSION_STRING_MAKE(value)     APP_VERSION_STRING_VALUE(value)
#define APP_VERSION_STRING                 APP_VERSION_STRING_MAKE(APP_VERSION_MAJOR) "." \
                                           APP_VERSION_STRING_MAKE(APP_VERSION_MINOR) "." \
                                           APP_VERSION_STRING_MAKE(APP_VERSION_PATCH)

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
