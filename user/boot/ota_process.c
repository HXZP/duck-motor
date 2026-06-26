#include "boot/ota_process.h"

#include "boot/boot_light.h"
#include "boot/boot_ota_def.h"
#include "boot/jump.h"
#include "boot/log.h"
#include "boot/ymodem_process.h"
#include "common/user_info.h"

#define OTA_PROCESS_APP_ADDRESS   YMODEM_PROCESS_APP_ADDRESS

/**
 * @brief Boot OTA 主状态。
 */
typedef enum
{
    OTA_PROCESS_STATE_PREPARE = 0u,       /**< 准备阶段。 */
    OTA_PROCESS_STATE_WAIT_OTA_COMMAND,   /**< 等待 OTA 命令阶段。 */
    OTA_PROCESS_STATE_START_OTA,          /**< 启动 YMODEM 阶段。 */
    OTA_PROCESS_STATE_POLL_OTA,           /**< 轮询 YMODEM 阶段。 */
    OTA_PROCESS_STATE_HANDLE_OTA_RESULT,  /**< 处理 OTA 结果阶段。 */
} ota_process_state_t;

/**
 * @brief Boot OTA 运行上下文。
 */
typedef struct
{
    int ota_result;                  /**< OTA 结果码，单位：无。 */
    uint8_t request_pending;         /**< OTA 请求挂起标志，单位：无。 */
    ota_process_state_t state;       /**< OTA 状态机当前状态。 */
} ota_process_context_t;

static ota_process_context_t s_ota_process_ctx = {0};

/**
 * @brief 打印 OTA 失败原因。
 * @param result OTA 结果码，单位：无。
 * @return void
 */
static void ota_process_log_failure(int result)
{
    if (result == BOOT_ERR_FULL)
    {
        printf("OTA failed: image size exceeds app flash space\r\n");
    }
    else if (result == BOOT_ERR_VERIFY)
    {
        printf("OTA failed: flash write or image verify error\r\n");
    }
    else if (result == BOOT_ERR_ABORTED)
    {
        printf("OTA aborted by sender or user\r\n");
    }
    else if (result == BOOT_ERR_TIMEOUT)
    {
        printf("OTA failed: session timeout\r\n");
    }
    else if (result == BOOT_ERR_PARAM)
    {
        printf("OTA failed: invalid parameter\r\n");
    }
    else if (result == BOOT_ERR_STATE)
    {
        printf("OTA failed: invalid runtime state\r\n");
    }
    else
    {
        printf("OTA failed: ret=%d\r\n", result);
    }
}

/**
 * @brief 重置 OTA 主上下文。
 * @return void
 */
static void ota_process_reset_context(void)
{
    s_ota_process_ctx.ota_result = BOOT_OK;
    YmodemProcess_ResetSession();
}

/**
 * @brief 根据 userinfo 的 OTA 标志决定是否跳转 App。
 * @return void
 */
static void ota_process_prepare_jump_or_ota(void)
{
    uint32_t ota_flag = USER_INFO_OTA_FLAG_BOOT;

    if (UserInfo_LoadOtaFlag(&ota_flag) != USER_INFO_OK)
    {
        ota_flag = USER_INFO_OTA_FLAG_BOOT;
    }

    printf("Boot ota flag=%lu\r\n", (unsigned long)ota_flag);
    if (ota_flag == USER_INFO_OTA_FLAG_APP)
    {
        if (BootJump_ToApplication(OTA_PROCESS_APP_ADDRESS) == 0u)
        {
            printf("App image invalid, stay in boot\r\n");
        }
    }
    else
    {
        printf("OTA flag detected, stay in boot\r\n");
        s_ota_process_ctx.request_pending = 1u;
    }

    s_ota_process_ctx.state = OTA_PROCESS_STATE_WAIT_OTA_COMMAND;
    BootLight_SetMode(BOOT_LIGHT_MODE_STANDBY);
}

/**
 * @brief 初始化 Boot OTA 处理模块。
 * @return void
 */
void OtaProcess_Init(void)
{
    s_ota_process_ctx.ota_result = BOOT_OK;
    s_ota_process_ctx.request_pending = 0u;
    s_ota_process_ctx.state = OTA_PROCESS_STATE_PREPARE;

    BootLight_Init();
    YmodemProcess_Init();
}

/**
 * @brief 标记收到 OTA 启动请求。
 * @return void
 */
void OtaProcess_SetRequestPending(void)
{
    s_ota_process_ctx.request_pending = 1u;
}

/**
 * @brief 查询当前是否处于 YMODEM 数据接收阶段。
 * @return uint8_t 非 0 表示允许接收 YMODEM 数据，0 表示未进入传输阶段。
 */
uint8_t OtaProcess_IsTransferEnabled(void)
{
    return YmodemProcess_IsTransferEnabled();
}

/**
 * @brief 向 OTA 接收缓冲区压入一段字节流。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int OtaProcess_PushRxBytes(const uint8_t *data, uint16_t len)
{
    return YmodemProcess_PushRxBytes(data, len);
}

/**
 * @brief 轮询推进 Boot OTA 状态机。
 * @return void
 */
void OtaProcess_Poll(void)
{
    int ret;

    BootLight_Poll();

    switch (s_ota_process_ctx.state)
    {
        case OTA_PROCESS_STATE_PREPARE:
        {
            ota_process_reset_context();
            ota_process_prepare_jump_or_ota();
            return;
        }

        case OTA_PROCESS_STATE_WAIT_OTA_COMMAND:
        {
            BootLight_SetMode(BOOT_LIGHT_MODE_STANDBY);
            if (s_ota_process_ctx.request_pending == 0u)
            {
                return;
            }

            s_ota_process_ctx.request_pending = 0u;
            s_ota_process_ctx.state = OTA_PROCESS_STATE_START_OTA;
            return;
        }

        case OTA_PROCESS_STATE_START_OTA:
        {
            BootLight_SetMode(BOOT_LIGHT_MODE_UPGRADING);
            ret = YmodemProcess_StartSession();
            if (ret != BOOT_OK)
            {
                ota_process_log_failure(ret);
                s_ota_process_ctx.state = OTA_PROCESS_STATE_WAIT_OTA_COMMAND;
                BootLight_SetMode(BOOT_LIGHT_MODE_STANDBY);
                return;
            }

            printf("OTA session started\r\n");
            s_ota_process_ctx.state = OTA_PROCESS_STATE_POLL_OTA;
            return;
        }

        case OTA_PROCESS_STATE_POLL_OTA:
        {
            BootLight_SetMode(BOOT_LIGHT_MODE_UPGRADING);
            ret = YmodemProcess_PollSession();
            if (ret == BOOT_ERR_BUSY)
            {
                return;
            }

            s_ota_process_ctx.ota_result = ret;
            s_ota_process_ctx.state = OTA_PROCESS_STATE_HANDLE_OTA_RESULT;
            return;
        }

        case OTA_PROCESS_STATE_HANDLE_OTA_RESULT:
        {
            BootLight_SetMode(BOOT_LIGHT_MODE_STANDBY);
            if (s_ota_process_ctx.ota_result == BOOT_OK)
            {
                ret = UserInfo_SaveOtaFlag(USER_INFO_OTA_FLAG_APP);
                if (ret != USER_INFO_OK)
                {
                    printf("OTA flag clear failed: ret=%d\r\n", ret);
                    s_ota_process_ctx.ota_result = BOOT_ERR_VERIFY;
                }
                else
                {
                    printf("OTA success, jump to app\r\n");
                    if (BootJump_ToApplication(OTA_PROCESS_APP_ADDRESS) == 0u)
                    {
                        printf("Updated app image invalid, stay in boot\r\n");
                    }
                }
            }

            if (s_ota_process_ctx.ota_result != BOOT_OK)
            {
                ota_process_log_failure(s_ota_process_ctx.ota_result);
            }

            ota_process_reset_context();
            s_ota_process_ctx.state = OTA_PROCESS_STATE_WAIT_OTA_COMMAND;
            return;
        }

        default:
        {
            s_ota_process_ctx.state = OTA_PROCESS_STATE_PREPARE;
            BootLight_SetMode(BOOT_LIGHT_MODE_STANDBY);
            return;
        }
    }
}
