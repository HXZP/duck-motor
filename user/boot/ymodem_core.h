#ifndef YMODEM_CORE_H
#define YMODEM_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define YMODEM_CORE_PACKET_SEQNO_INDEX       1u
#define YMODEM_CORE_PACKET_SEQNO_COMP_INDEX  2u
#define YMODEM_CORE_PACKET_HEADER            3u
#define YMODEM_CORE_PACKET_TRAILER           2u
#define YMODEM_CORE_PACKET_OVERHEAD          (YMODEM_CORE_PACKET_HEADER + YMODEM_CORE_PACKET_TRAILER)
#define YMODEM_CORE_PACKET_SIZE              128u
#define YMODEM_CORE_PACKET_1K_SIZE           1024u
#define YMODEM_CORE_FILE_NAME_LENGTH         128u
#define YMODEM_CORE_FILE_SIZE_LENGTH         16u

#define YMODEM_CORE_SOH                      0x01u
#define YMODEM_CORE_STX                      0x02u
#define YMODEM_CORE_EOT                      0x04u
#define YMODEM_CORE_ACK                      0x06u
#define YMODEM_CORE_NAK                      0x15u
#define YMODEM_CORE_CA                       0x18u
#define YMODEM_CORE_CRC16_REQUEST            0x43u
#define YMODEM_CORE_ABORT1                   0x41u
#define YMODEM_CORE_ABORT2                   0x61u

#define YMODEM_CORE_NAK_TIMEOUT_MS           50u
#define YMODEM_CORE_SESSION_TIMEOUT_MS       (90u * 1000u)
#define YMODEM_CORE_RETRY_DELAY_MS           50u
#define YMODEM_CORE_MAX_ERRORS               5u

typedef int (*ymodem_core_read_byte_fn_t)(uint8_t *byte, void *user);
typedef int (*ymodem_core_write_fn_t)(const uint8_t *data, uint32_t len, void *user);
typedef uint32_t (*ymodem_core_get_tick_ms_fn_t)(void *user);

typedef struct ymodem_core_event ymodem_core_event_t;
typedef int (*ymodem_core_event_handler_fn_t)(const ymodem_core_event_t *event, void *user);

/**
 * @brief YMODEM 底层收发端口。
 */
typedef struct
{
    ymodem_core_read_byte_fn_t read_byte;       /**< 读取单字节函数。 */
    ymodem_core_write_fn_t write_bytes;         /**< 写入字节流函数。 */
    ymodem_core_get_tick_ms_fn_t get_tick_ms;   /**< 获取毫秒计时函数。 */
    void *user;                                 /**< 端口用户上下文。 */
} ymodem_core_port_t;

/**
 * @brief YMODEM 收包状态。
 */
typedef enum
{
    YMODEM_CORE_PACKET_STATE_WAIT_START = 0u,
    YMODEM_CORE_PACKET_STATE_WAIT_CA_CONFIRM,
    YMODEM_CORE_PACKET_STATE_WAIT_REST,
} ymodem_core_packet_state_t;

/**
 * @brief YMODEM 会话状态。
 */
typedef enum
{
    YMODEM_CORE_SESSION_STATE_IDLE = 0u,
    YMODEM_CORE_SESSION_STATE_WAIT_HEADER,
    YMODEM_CORE_SESSION_STATE_WAIT_HEADER_COMMIT,
    YMODEM_CORE_SESSION_STATE_RECEIVE_DATA,
    YMODEM_CORE_SESSION_STATE_WAIT_DATA_COMMIT,
    YMODEM_CORE_SESSION_STATE_WAIT_EOT_CONFIRM, /**< 等待第二个 EOT 阶段。 */
    YMODEM_CORE_SESSION_STATE_WAIT_END_HEADER,  /**< 等待结束空头包阶段。 */
    YMODEM_CORE_SESSION_STATE_FINISHED,
    YMODEM_CORE_SESSION_STATE_ABORTED,
    YMODEM_CORE_SESSION_STATE_ERROR,
} ymodem_core_session_state_t;

/**
 * @brief YMODEM 事件类型。
 */
typedef enum
{
    YMODEM_CORE_EVENT_NONE = 0u,
    YMODEM_CORE_EVENT_HEADER,
    YMODEM_CORE_EVENT_DATA,
    YMODEM_CORE_EVENT_FINISH,
    YMODEM_CORE_EVENT_ABORT,
    YMODEM_CORE_EVENT_ERROR,
} ymodem_core_event_type_t;

/**
 * @brief YMODEM 事件。
 */
struct ymodem_core_event
{
    ymodem_core_event_type_t type;  /**< 事件类型。 */
    uint8_t packet_index;           /**< 数据包序号，单位：包。 */
    const uint8_t *data;            /**< 数据包载荷。 */
    uint32_t data_length;           /**< 数据包载荷长度，单位：字节。 */
    const char *file_name;          /**< 文件名字符串。 */
    uint32_t file_size;             /**< 文件总长度，单位：字节。 */
    int result;                     /**< 事件结果码。 */
};

/**
 * @brief YMODEM 协议上下文。
 */
typedef struct
{
    ymodem_core_port_t port;                         /**< 底层端口。 */
    ymodem_core_event_handler_fn_t event_handler;    /**< 事件处理函数。 */
    void *event_handler_user;                        /**< 事件处理函数上下文。 */
    ymodem_core_packet_state_t packet_state;         /**< 收包状态。 */
    ymodem_core_session_state_t session_state;       /**< 会话状态。 */
    int result;                                      /**< 最终结果码。 */
    int error_count;                                 /**< 连续错误次数，单位：次。 */
    int packets_received;                            /**< 已收包数量，单位：包。 */
    uint32_t timeout_tick;                           /**< 会话超时基准，单位：毫秒。 */
    uint32_t next_action_tick;                       /**< 下一次动作时间，单位：毫秒。 */
    uint32_t packet_deadline_tick;                   /**< 当前收包截止时间，单位：毫秒。 */
    uint32_t file_size;                              /**< 文件总长度，单位：字节。 */
    uint16_t last_packet_size;                       /**< 上一个包长度，单位：字节。 */
    uint16_t packet_size;                            /**< 当前包长度，单位：字节。 */
    uint16_t packet_bytes_received;                  /**< 当前包已收字节数，单位：字节。 */
    uint8_t active;                                  /**< 会话激活标志。 */
    uint8_t session_begin;                           /**< 文件数据开始标志。 */
    uint8_t pending_commit;                          /**< 等待业务提交标志。 */
    ymodem_core_event_type_t pending_event_type;     /**< 等待提交的事件类型。 */
    char file_name[YMODEM_CORE_FILE_NAME_LENGTH];    /**< 文件名缓存。 */
    char file_size_text[YMODEM_CORE_FILE_SIZE_LENGTH]; /**< 文件大小文本缓存。 */
    uint8_t packet_data[YMODEM_CORE_PACKET_1K_SIZE + YMODEM_CORE_PACKET_OVERHEAD]; /**< 包缓存。 */
} ymodem_core_t;

/**
 * @brief 初始化 YMODEM 协议上下文。
 * @param ctx 协议上下文。
 * @param port 底层收发端口。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_Init(ymodem_core_t *ctx, const ymodem_core_port_t *port);

/**
 * @brief 启动 YMODEM 接收会话。
 * @param ctx 协议上下文。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_Start(ymodem_core_t *ctx);

/**
 * @brief 注册 YMODEM 事件处理函数。
 * @param ctx 协议上下文。
 * @param handler 事件处理函数。
 * @param user 事件处理函数上下文。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_RegisterEventHandler(ymodem_core_t *ctx,
                                    ymodem_core_event_handler_fn_t handler,
                                    void *user);

/**
 * @brief 推进一次 YMODEM 状态机。
 * @param ctx 协议上下文。
 * @param event 事件输出缓冲区。
 * @return int 成功结束返回 BOOT_OK，进行中返回 BOOT_ERR_BUSY，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_Poll(ymodem_core_t *ctx, ymodem_core_event_t *event);

/**
 * @brief 提交当前 HEADER 或 DATA 事件的业务处理结果。
 * @param ctx 协议上下文。
 * @param result 业务处理结果。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemCore_Commit(ymodem_core_t *ctx, int result);

/**
 * @brief 查询当前会话是否激活。
 * @param ctx 协议上下文。
 * @return uint8_t 1 表示激活，0 表示未激活。
 */
uint8_t YmodemCore_IsActive(const ymodem_core_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* YMODEM_CORE_H */
