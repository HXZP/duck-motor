#include "app/app_version.h"

#define APP_VERSION_STRING    "1.0.0"  /**< App 版本号字符串。 */

/**
 * @brief 获取当前 App 版本号。
 * @return app_version_t 当前 App 版本号。
 */
app_version_t AppVersion_Get(void)
{
    app_version_t version = {0};

    version.major = (uint8_t)APP_VERSION_MAJOR;
    version.minor = (uint8_t)APP_VERSION_MINOR;
    version.patch = (uint8_t)APP_VERSION_PATCH;

    return version;
}

/**
 * @brief 获取当前 App 版本号字符串。
 * @return const char * 当前 App 版本号字符串。
 */
const char *AppVersion_GetString(void)
{
    return APP_VERSION_STRING;
}
