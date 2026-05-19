#include "boot/ymodem_core.h"

#include "boot/boot_ota_def.h"
#include "boot/crc16.h"

#include <string.h>

/**
 * @brief 收包状态机输出事件。
 */
typedef enum
{
    YMODEM_CORE_PACKET_EVENT_PENDING = 0u,
    YMODEM_CORE_PACKET_EVENT_DATA,
    YMODEM_CORE_PACKET_EVENT_EOT,
    YMODEM_CORE_PACKET_EVENT_SENDER_ABORT,
    YMODEM_CORE_PACKET_EVENT_USER_ABORT,
    YMODEM_CORE_PACKET_EVENT_ERROR,
} ymodem_core_packet_event_t;

/**
 * @brief 清空一次协议事件。
 * @param event 协议事件。
 * @return void
 */
static void ymodem_core_clear_event(ymodem_core_event_t *event)
{
    if (event == NULL)
    {
        return;
    }

    memset(event, 0, sizeof(*event));
    event->type = YMODEM_CORE_EVENT_NONE;
}

/**
 * @brief 获取当前毫秒计时。
 * @param ctx 协议上下文。
 * @return uint32_t 当前毫秒计数，单位：毫秒。
 */
static uint32_t ymodem_core_get_tick_ms(const ymodem_core_t *ctx)
{
    if ((ctx == NULL) || (ctx->port.get_tick_ms == NULL))
    {
        return 0u;
    }

    return ctx->port.get_tick_ms(ctx->port.user);
}

/**
 * @brief 判断指定截止时间是否到达。
 * @param ctx 协议上下文。
 * @param deadline_tick 截止时间，单位：毫秒。
 * @return uint8_t 1 表示已到达，0 表示未到达。
 */
static uint8_t ymodem_core_is_deadline_reached(const ymodem_core_t *ctx, uint32_t deadline_tick)
{
    if ((int32_t)(ymodem_core_get_tick_ms(ctx) - deadline_tick) >= 0)
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief 向发送端输出协议字节流。
 * @param ctx 协议上下文。
 * @param data 输出数据缓冲区。
 * @param len 输出数据长度，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_core_send_bytes(ymodem_core_t *ctx, const uint8_t *data, uint32_t len)
{
    if ((ctx == NULL) || (data == NULL) || (len == 0u))
    {
        return BOOT_ERR_PARAM;
    }

    if (ctx->port.write_bytes == NULL)
    {
        return BOOT_ERR_STATE;
    }

    return ctx->port.write_bytes(data, len, ctx->port.user);
}

/**
 * @brief 发送单字节协议控制字符。
 * @param ctx 协议上下文。
 * @param value 控制字符。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_core_send_control_byte(ymodem_core_t *ctx, uint8_t value)
{
    return ymodem_core_send_bytes(ctx, &value, 1u);
}

/**
 * @brief 发送取消序列。
 * @param ctx 协议上下文。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_core_send_cancel_sequence(ymodem_core_t *ctx)
{
    uint8_t cancel_buf[2] = {YMODEM_CORE_CA, YMODEM_CORE_CA};

    return ymodem_core_send_bytes(ctx, cancel_buf, 2u);
}

/**
 * @brief 重置当前收包状态。
 * @param ctx 协议上下文。
 * @return void
 */
static void ymodem_core_reset_packet_state(ymodem_core_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->packet_state = YMODEM_CORE_PACKET_STATE_WAIT_START;
    ctx->packet_size = 0u;
    ctx->packet_bytes_received = 0u;
    ctx->packet_deadline_tick = ymodem_core_get_tick_ms(ctx) + YMODEM_CORE_NAK_TIMEOUT_MS;
}

/**
 * @brief 结束当前协议会话。
 * @param ctx 协议上下文。
 * @param result 最终结果码。
 * @return void
 */
static void ymodem_core_finish(ymodem_core_t *ctx, int result)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->active = 0u;
    ctx->pending_commit = 0u;
    ctx->pending_event_type = YMODEM_CORE_EVENT_NONE;
    ctx->result = result;

    if (result == BOOT_OK)
    {
        ctx->session_state = YMODEM_CORE_SESSION_STATE_FINISHED;
    }
    else if (result == BOOT_ERR_ABORTED)
    {
        ctx->session_state = YMODEM_CORE_SESSION_STATE_ABORTED;
    }
    else
    {
        ctx->session_state = YMODEM_CORE_SESSION_STATE_ERROR;
    }
}

/**
 * @brief 分发协议事件到上层处理函数。
 * @param ctx 协议上下文。
 * @param event 协议事件。
 * @return int 成功结束返回 BOOT_OK，进行中返回 BOOT_ERR_BUSY，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_core_dispatch_event(ymodem_core_t *ctx, ymodem_core_event_t *event)
{
    int handler_result;
    int commit_result;

    if ((ctx == NULL) || (event == NULL))
    {
        return BOOT_ERR_PARAM;
    }

    if (ctx->event_handler == NULL)
    {
        return BOOT_OK;
    }

    handler_result = ctx->event_handler(event, ctx->event_handler_user);
    if ((event->type == YMODEM_CORE_EVENT_HEADER) ||
        (event->type == YMODEM_CORE_EVENT_DATA))
    {
        commit_result = YmodemCore_Commit(ctx, handler_result);
        if (commit_result != BOOT_OK)
        {
            event->result = commit_result;
            return commit_result;
        }

        return BOOT_ERR_BUSY;
    }

    if (event->type == YMODEM_CORE_EVENT_FINISH)
    {
        return handler_result;
    }

    if ((event->type == YMODEM_CORE_EVENT_ABORT) ||
        (event->type == YMODEM_CORE_EVENT_ERROR))
    {
        if (handler_result != BOOT_OK)
        {
            return handler_result;
        }

        return event->result;
    }

    return BOOT_ERR_BUSY;
}

/**
 * @brief 处理收包起始字节。
 * @param ctx 协议上下文。
 * @param byte 起始字节。
 * @return ymodem_core_packet_event_t 收包事件。
 */
static ymodem_core_packet_event_t ymodem_core_handle_start_byte(ymodem_core_t *ctx, uint8_t byte)
{
    if (ctx == NULL)
    {
        return YMODEM_CORE_PACKET_EVENT_ERROR;
    }

    if (byte == YMODEM_CORE_SOH)
    {
        ctx->packet_size = YMODEM_CORE_PACKET_SIZE;
        ctx->packet_data[0] = byte;
        ctx->packet_bytes_received = 1u;
        ctx->packet_state = YMODEM_CORE_PACKET_STATE_WAIT_REST;
        ctx->packet_deadline_tick = ymodem_core_get_tick_ms(ctx) + YMODEM_CORE_NAK_TIMEOUT_MS;
        return YMODEM_CORE_PACKET_EVENT_PENDING;
    }

    if (byte == YMODEM_CORE_STX)
    {
        ctx->packet_size = YMODEM_CORE_PACKET_1K_SIZE;
        ctx->packet_data[0] = byte;
        ctx->packet_bytes_received = 1u;
        ctx->packet_state = YMODEM_CORE_PACKET_STATE_WAIT_REST;
        ctx->packet_deadline_tick = ymodem_core_get_tick_ms(ctx) + YMODEM_CORE_NAK_TIMEOUT_MS;
        return YMODEM_CORE_PACKET_EVENT_PENDING;
    }

    if (byte == YMODEM_CORE_EOT)
    {
        ymodem_core_reset_packet_state(ctx);
        return YMODEM_CORE_PACKET_EVENT_EOT;
    }

    if (byte == YMODEM_CORE_CA)
    {
        ctx->packet_state = YMODEM_CORE_PACKET_STATE_WAIT_CA_CONFIRM;
        ctx->packet_deadline_tick = ymodem_core_get_tick_ms(ctx) + YMODEM_CORE_NAK_TIMEOUT_MS;
        return YMODEM_CORE_PACKET_EVENT_PENDING;
    }

    if ((byte == YMODEM_CORE_ABORT1) || (byte == YMODEM_CORE_ABORT2))
    {
        ymodem_core_reset_packet_state(ctx);
        return YMODEM_CORE_PACKET_EVENT_USER_ABORT;
    }

    ymodem_core_reset_packet_state(ctx);
    return YMODEM_CORE_PACKET_EVENT_ERROR;
}

/**
 * @brief 将十进制文本解析为 32 位无符号整数。
 * @param text 十进制文本。
 * @param value 输出数值，单位：无。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_core_parse_u32_dec(const char *text, uint32_t *value)
{
    uint32_t result = 0u;
    const char *cursor;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0'))
    {
        return BOOT_ERR_PARAM;
    }

    cursor = text;
    while (*cursor != '\0')
    {
        uint32_t digit;

        if ((*cursor < '0') || (*cursor > '9'))
        {
            return BOOT_ERR_PARAM;
        }

        digit = (uint32_t)(*cursor - '0');
        if (result > ((0xFFFFFFFFu - digit) / 10u))
        {
            return BOOT_ERR_PARAM;
        }

        result = (result * 10u) + digit;
        cursor++;
    }

    *value = result;
    return BOOT_OK;
}

/**
 * @brief 解析 YMODEM 文件头包。
 * @param ctx 协议上下文。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_core_parse_header(ymodem_core_t *ctx)
{
    const uint8_t *file_ptr;
    const uint8_t *packet_end;
    uint32_t index = 0u;
    uint32_t packet_size;

    if (ctx == NULL)
    {
        return BOOT_ERR_PARAM;
    }

    memset(ctx->file_name, 0, sizeof(ctx->file_name));
    memset(ctx->file_size_text, 0, sizeof(ctx->file_size_text));
    ctx->file_size = 0u;
    packet_size = (uint32_t)ctx->last_packet_size;

    packet_end = &ctx->packet_data[YMODEM_CORE_PACKET_HEADER + packet_size];
    file_ptr = &ctx->packet_data[YMODEM_CORE_PACKET_HEADER];

    while ((file_ptr < packet_end) &&
           (*file_ptr != 0u) &&
           (index < (YMODEM_CORE_FILE_NAME_LENGTH - 1u)))
    {
        ctx->file_name[index] = (char)(*file_ptr);
        index++;
        file_ptr++;
    }
    ctx->file_name[index] = '\0';

    if ((ctx->file_name[0] == '\0') || (file_ptr >= packet_end) || (*file_ptr != 0u))
    {
        return BOOT_ERR_PARAM;
    }

    file_ptr++;
    index = 0u;
    while ((file_ptr < packet_end) &&
           (*file_ptr != ' ') &&
           (*file_ptr != 0u) &&
           (index < (YMODEM_CORE_FILE_SIZE_LENGTH - 1u)))
    {
        ctx->file_size_text[index] = (char)(*file_ptr);
        index++;
        file_ptr++;
    }
    ctx->file_size_text[index] = '\0';

    return ymodem_core_parse_u32_dec(ctx->file_size_text, &ctx->file_size);
}

/**
 * @brief 推进一次收包状态机。
 * @param ctx 协议上下文。
 * @param length 当前包有效载荷长度输出，单位：字节。
 * @return ymodem_core_packet_event_t 收包事件。
 */
static ymodem_core_packet_event_t ymodem_core_receive_packet_step(ymodem_core_t *ctx, int32_t *length)
{
    int read_ret;
    uint8_t byte = 0u;
    uint16_t total_length;
    uint16_t computed_crc;
    uint16_t received_crc;
    ymodem_core_packet_event_t packet_event;

    if ((ctx == NULL) || (length == NULL))
    {
        return YMODEM_CORE_PACKET_EVENT_ERROR;
    }

    *length = 0;

    while (1)
    {
        switch (ctx->packet_state)
        {
            case YMODEM_CORE_PACKET_STATE_WAIT_START:
            {
                read_ret = ctx->port.read_byte(&byte, ctx->port.user);
                if (read_ret != BOOT_OK)
                {
                    if (ymodem_core_is_deadline_reached(ctx, ctx->packet_deadline_tick) == 0u)
                    {
                        return YMODEM_CORE_PACKET_EVENT_PENDING;
                    }

                    ymodem_core_reset_packet_state(ctx);
                    return YMODEM_CORE_PACKET_EVENT_ERROR;
                }

                packet_event = ymodem_core_handle_start_byte(ctx, byte);
                if (packet_event != YMODEM_CORE_PACKET_EVENT_PENDING)
                {
                    return packet_event;
                }

                break;
            }

            case YMODEM_CORE_PACKET_STATE_WAIT_CA_CONFIRM:
            {
                read_ret = ctx->port.read_byte(&byte, ctx->port.user);
                if (read_ret != BOOT_OK)
                {
                    if (ymodem_core_is_deadline_reached(ctx, ctx->packet_deadline_tick) == 0u)
                    {
                        return YMODEM_CORE_PACKET_EVENT_PENDING;
                    }

                    ymodem_core_reset_packet_state(ctx);
                    return YMODEM_CORE_PACKET_EVENT_ERROR;
                }

                ymodem_core_reset_packet_state(ctx);
                if (byte == YMODEM_CORE_CA)
                {
                    *length = -1;
                    return YMODEM_CORE_PACKET_EVENT_SENDER_ABORT;
                }

                return YMODEM_CORE_PACKET_EVENT_ERROR;
            }

            case YMODEM_CORE_PACKET_STATE_WAIT_REST:
            {
                total_length = (uint16_t)(ctx->packet_size + YMODEM_CORE_PACKET_OVERHEAD);
                while (ctx->packet_bytes_received < total_length)
                {
                    read_ret = ctx->port.read_byte(&byte, ctx->port.user);
                    if (read_ret != BOOT_OK)
                    {
                        break;
                    }

                    ctx->packet_data[ctx->packet_bytes_received] = byte;
                    ctx->packet_bytes_received++;
                    ctx->packet_deadline_tick = ymodem_core_get_tick_ms(ctx) + YMODEM_CORE_NAK_TIMEOUT_MS;
                }

                if (ctx->packet_bytes_received < total_length)
                {
                    if (ymodem_core_is_deadline_reached(ctx, ctx->packet_deadline_tick) == 0u)
                    {
                        return YMODEM_CORE_PACKET_EVENT_PENDING;
                    }

                    ymodem_core_reset_packet_state(ctx);
                    return YMODEM_CORE_PACKET_EVENT_ERROR;
                }

                if (ctx->packet_data[YMODEM_CORE_PACKET_SEQNO_INDEX] !=
                    ((ctx->packet_data[YMODEM_CORE_PACKET_SEQNO_COMP_INDEX] ^ 0xFFu) & 0xFFu))
                {
                    ymodem_core_reset_packet_state(ctx);
                    return YMODEM_CORE_PACKET_EVENT_ERROR;
                }

                computed_crc = BootCrc16_CcittCalc(&ctx->packet_data[YMODEM_CORE_PACKET_HEADER],
                                                   (uint32_t)ctx->packet_size);
                received_crc = (uint16_t)(((uint16_t)ctx->packet_data[ctx->packet_size + 3u] << 8) |
                                           ctx->packet_data[ctx->packet_size + 4u]);
                if (computed_crc != received_crc)
                {
                    ymodem_core_reset_packet_state(ctx);
                    return YMODEM_CORE_PACKET_EVENT_ERROR;
                }

                *length = (int32_t)ctx->packet_size;
                ctx->last_packet_size = ctx->packet_size;
                ymodem_core_reset_packet_state(ctx);
                return YMODEM_CORE_PACKET_EVENT_DATA;
            }

            default:
            {
                ymodem_core_reset_packet_state(ctx);
                return YMODEM_CORE_PACKET_EVENT_ERROR;
            }
        }
    }
}

/**
 * @brief 初始化 YMODEM 协议上下文。
 * @param ctx 协议上下文。
 * @param port 底层收发端口。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_Init(ymodem_core_t *ctx, const ymodem_core_port_t *port)
{
    if ((ctx == NULL) || (port == NULL))
    {
        return BOOT_ERR_PARAM;
    }

    if ((port->read_byte == NULL) || (port->write_bytes == NULL) || (port->get_tick_ms == NULL))
    {
        return BOOT_ERR_PARAM;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->port = *port;
    ctx->session_state = YMODEM_CORE_SESSION_STATE_IDLE;
    ctx->packet_state = YMODEM_CORE_PACKET_STATE_WAIT_START;
    ctx->result = BOOT_OK;
    return BOOT_OK;
}

/**
 * @brief 启动 YMODEM 接收会话。
 * @param ctx 协议上下文。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_Start(ymodem_core_t *ctx)
{
    int ret;
    ymodem_core_port_t port;
    ymodem_core_event_handler_fn_t event_handler;
    void *event_handler_user;

    if (ctx == NULL)
    {
        return BOOT_ERR_PARAM;
    }

    port = ctx->port;
    event_handler = ctx->event_handler;
    event_handler_user = ctx->event_handler_user;
    memset(ctx, 0, sizeof(*ctx));
    ctx->port = port;
    ctx->event_handler = event_handler;
    ctx->event_handler_user = event_handler_user;
    ctx->active = 1u;
    ctx->result = BOOT_OK;
    ctx->session_state = YMODEM_CORE_SESSION_STATE_WAIT_HEADER;
    ctx->next_action_tick = ymodem_core_get_tick_ms(ctx);
    ctx->timeout_tick = ymodem_core_get_tick_ms(ctx);
    ymodem_core_reset_packet_state(ctx);

    ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_CRC16_REQUEST);
    if (ret != BOOT_OK)
    {
        ymodem_core_finish(ctx, ret);
        return ret;
    }

    return BOOT_OK;
}

/**
 * @brief 注册 YMODEM 事件处理函数。
 * @param ctx 协议上下文。
 * @param handler 事件处理函数。
 * @param user 事件处理函数上下文。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_RegisterEventHandler(ymodem_core_t *ctx,
                                    ymodem_core_event_handler_fn_t handler,
                                    void *user)
{
    if (ctx == NULL)
    {
        return BOOT_ERR_PARAM;
    }

    ctx->event_handler = handler;
    ctx->event_handler_user = user;
    return BOOT_OK;
}

/**
 * @brief 推进一次 YMODEM 状态机。
 * @param ctx 协议上下文。
 * @param event 事件输出缓冲区。
 * @return int 成功结束返回 BOOT_OK，进行中返回 BOOT_ERR_BUSY，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_Poll(ymodem_core_t *ctx, ymodem_core_event_t *event)
{
    int ret;
    int32_t packet_length = 0;
    ymodem_core_packet_event_t packet_event;

    if ((ctx == NULL) || (event == NULL))
    {
        return BOOT_ERR_PARAM;
    }

    ymodem_core_clear_event(event);
    if (ctx->active == 0u)
    {
        return BOOT_ERR_STATE;
    }

    if (ctx->pending_commit != 0u)
    {
        return BOOT_ERR_STATE;
    }

    if (ymodem_core_is_deadline_reached(ctx, ctx->timeout_tick + YMODEM_CORE_SESSION_TIMEOUT_MS) != 0u)
    {
        ymodem_core_send_cancel_sequence(ctx);
        ymodem_core_finish(ctx, BOOT_ERR_TIMEOUT);
        event->type = YMODEM_CORE_EVENT_ERROR;
        event->result = BOOT_ERR_TIMEOUT;
        return ymodem_core_dispatch_event(ctx, event);
    }

    if (ymodem_core_is_deadline_reached(ctx, ctx->next_action_tick) == 0u)
    {
        return BOOT_ERR_BUSY;
    }

    packet_event = ymodem_core_receive_packet_step(ctx, &packet_length);
    switch (packet_event)
    {
        case YMODEM_CORE_PACKET_EVENT_PENDING:
        {
            return BOOT_ERR_BUSY;
        }

        case YMODEM_CORE_PACKET_EVENT_DATA:
        {
            ctx->error_count = 0;
            if ((ctx->packet_data[YMODEM_CORE_PACKET_SEQNO_INDEX] & 0xFFu) !=
                ((uint8_t)ctx->packets_received & 0xFFu))
            {
                ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_NAK);
                if (ret != BOOT_OK)
                {
                    ymodem_core_finish(ctx, ret);
                    event->type = YMODEM_CORE_EVENT_ERROR;
                    event->result = ret;
                    return ymodem_core_dispatch_event(ctx, event);
                }

                return BOOT_ERR_BUSY;
            }

            if (ctx->packets_received == 0)
            {
                if (ctx->packet_data[YMODEM_CORE_PACKET_HEADER] == 0u)
                {
                    ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_ACK);
                    if (ret != BOOT_OK)
                    {
                        ymodem_core_finish(ctx, ret);
                        event->type = YMODEM_CORE_EVENT_ERROR;
                        event->result = ret;
                        return ymodem_core_dispatch_event(ctx, event);
                    }

                    ymodem_core_finish(ctx, BOOT_OK);
                    event->type = YMODEM_CORE_EVENT_FINISH;
                    event->file_name = ctx->file_name;
                    event->file_size = ctx->file_size;
                    event->result = BOOT_OK;
                    return ymodem_core_dispatch_event(ctx, event);
                }

                ret = ymodem_core_parse_header(ctx);
                if (ret != BOOT_OK)
                {
                    ymodem_core_send_cancel_sequence(ctx);
                    ymodem_core_finish(ctx, ret);
                    event->type = YMODEM_CORE_EVENT_ERROR;
                    event->result = ret;
                    return ymodem_core_dispatch_event(ctx, event);
                }

                ctx->pending_commit = 1u;
                ctx->pending_event_type = YMODEM_CORE_EVENT_HEADER;
                ctx->session_state = YMODEM_CORE_SESSION_STATE_WAIT_HEADER_COMMIT;
                event->type = YMODEM_CORE_EVENT_HEADER;
                event->packet_index = (uint8_t)ctx->packets_received;
                event->file_name = ctx->file_name;
                event->file_size = ctx->file_size;
                event->result = BOOT_OK;
                return ymodem_core_dispatch_event(ctx, event);
            }

            ctx->pending_commit = 1u;
            ctx->pending_event_type = YMODEM_CORE_EVENT_DATA;
            ctx->session_state = YMODEM_CORE_SESSION_STATE_WAIT_DATA_COMMIT;
            event->type = YMODEM_CORE_EVENT_DATA;
            event->packet_index = (uint8_t)ctx->packets_received;
            event->data = &ctx->packet_data[YMODEM_CORE_PACKET_HEADER];
            event->data_length = (uint32_t)packet_length;
            event->file_name = ctx->file_name;
            event->file_size = ctx->file_size;
            event->result = BOOT_OK;
            return ymodem_core_dispatch_event(ctx, event);
        }

        case YMODEM_CORE_PACKET_EVENT_EOT:
        {
            if (ctx->session_state != YMODEM_CORE_SESSION_STATE_WAIT_EOT_CONFIRM)
            {
                ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_NAK);
                if (ret != BOOT_OK)
                {
                    ymodem_core_finish(ctx, ret);
                    event->type = YMODEM_CORE_EVENT_ERROR;
                    event->result = ret;
                    return ymodem_core_dispatch_event(ctx, event);
                }

                ctx->session_state = YMODEM_CORE_SESSION_STATE_WAIT_EOT_CONFIRM;
                ymodem_core_reset_packet_state(ctx);
                return BOOT_ERR_BUSY;
            }

            ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_ACK);
            if (ret == BOOT_OK)
            {
                ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_CRC16_REQUEST);
            }

            if (ret != BOOT_OK)
            {
                ymodem_core_finish(ctx, ret);
                event->type = YMODEM_CORE_EVENT_ERROR;
                event->result = ret;
                return ymodem_core_dispatch_event(ctx, event);
            }

            ctx->packets_received = 0;
            ctx->session_state = YMODEM_CORE_SESSION_STATE_WAIT_END_HEADER;
            ymodem_core_reset_packet_state(ctx);
            return BOOT_ERR_BUSY;
        }

        case YMODEM_CORE_PACKET_EVENT_SENDER_ABORT:
        case YMODEM_CORE_PACKET_EVENT_USER_ABORT:
        {
            if (packet_event == YMODEM_CORE_PACKET_EVENT_SENDER_ABORT)
            {
                ymodem_core_send_control_byte(ctx, YMODEM_CORE_ACK);
            }
            else
            {
                ymodem_core_send_cancel_sequence(ctx);
            }

            ymodem_core_finish(ctx, BOOT_ERR_ABORTED);
            event->type = YMODEM_CORE_EVENT_ABORT;
            event->result = BOOT_ERR_ABORTED;
            return ymodem_core_dispatch_event(ctx, event);
        }

        case YMODEM_CORE_PACKET_EVENT_ERROR:
        default:
        {
            if (ctx->session_begin != 0u)
            {
                ctx->error_count++;
            }

            if ((uint32_t)ctx->error_count > YMODEM_CORE_MAX_ERRORS)
            {
                ymodem_core_send_cancel_sequence(ctx);
                ymodem_core_finish(ctx, BOOT_ERR);
                event->type = YMODEM_CORE_EVENT_ERROR;
                event->result = BOOT_ERR;
                return ymodem_core_dispatch_event(ctx, event);
            }

            ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_CRC16_REQUEST);
            if (ret != BOOT_OK)
            {
                ymodem_core_finish(ctx, ret);
                event->type = YMODEM_CORE_EVENT_ERROR;
                event->result = ret;
                return ymodem_core_dispatch_event(ctx, event);
            }

            ctx->next_action_tick = ymodem_core_get_tick_ms(ctx) + YMODEM_CORE_RETRY_DELAY_MS;
            return BOOT_ERR_BUSY;
        }
    }
}

/**
 * @brief 提交当前 HEADER 或 DATA 事件的业务处理结果。
 * @param ctx 协议上下文。
 * @param result 业务处理结果。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_Commit(ymodem_core_t *ctx, int result)
{
    int ret;

    if (ctx == NULL)
    {
        return BOOT_ERR_PARAM;
    }

    if ((ctx->active == 0u) || (ctx->pending_commit == 0u))
    {
        return BOOT_ERR_STATE;
    }

    if (result != BOOT_OK)
    {
        ymodem_core_send_cancel_sequence(ctx);
        ymodem_core_finish(ctx, result);
        return result;
    }

    if (ctx->pending_event_type == YMODEM_CORE_EVENT_HEADER)
    {
        ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_ACK);
        if (ret != BOOT_OK)
        {
            ymodem_core_finish(ctx, ret);
            return ret;
        }

        ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_CRC16_REQUEST);
        if (ret != BOOT_OK)
        {
            ymodem_core_finish(ctx, ret);
            return ret;
        }

        ctx->packets_received++;
        ctx->session_begin = 1u;
        ctx->timeout_tick = ymodem_core_get_tick_ms(ctx);
        ctx->session_state = YMODEM_CORE_SESSION_STATE_RECEIVE_DATA;
    }
    else if (ctx->pending_event_type == YMODEM_CORE_EVENT_DATA)
    {
        ret = ymodem_core_send_control_byte(ctx, YMODEM_CORE_ACK);
        if (ret != BOOT_OK)
        {
            ymodem_core_finish(ctx, ret);
            return ret;
        }

        ctx->packets_received++;
        ctx->timeout_tick = ymodem_core_get_tick_ms(ctx);
        ctx->session_state = YMODEM_CORE_SESSION_STATE_RECEIVE_DATA;
    }
    else
    {
        return BOOT_ERR_STATE;
    }

    ctx->pending_commit = 0u;
    ctx->pending_event_type = YMODEM_CORE_EVENT_NONE;
    return BOOT_OK;
}

/**
 * @brief 查询当前会话是否激活。
 * @param ctx 协议上下文。
 * @return uint8_t 1 表示激活，0 表示未激活。
 */
uint8_t YmodemCore_IsActive(const ymodem_core_t *ctx)
{
    if (ctx == NULL)
    {
        return 0u;
    }

    return ctx->active;
}
