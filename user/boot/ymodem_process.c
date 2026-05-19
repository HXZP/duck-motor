#include "boot/ymodem_process.h"

#include "boot/boot_ota_def.h"
#include "boot/can_protocol.h"
#include "boot/log.h"
#include "boot/queue.h"
#include "common/user_info.h"

#include <string.h>

#define YMODEM_PROCESS_CAN_DATA_QUEUE_MARGIN   4u
#define YMODEM_PROCESS_CAN_DATA_QUEUE_MAX_SIZE \
    (YMODEM_CORE_PACKET_1K_SIZE + YMODEM_CORE_PACKET_OVERHEAD + YMODEM_PROCESS_CAN_DATA_QUEUE_MARGIN)

/**
 * @brief YMODEM 处理上下文。
 */
typedef struct
{
    uint8_t rx_buffer[YMODEM_PROCESS_CAN_DATA_QUEUE_MAX_SIZE]; /**< CAN 接收字节队列缓冲区。 */
    BootQueue_t rx_queue;                                     /**< CAN 接收字节队列。 */
    ymodem_process_file_info_t file_info;                     /**< 当前文件信息。 */
    uint32_t flash_destination;                               /**< 当前 Flash 写入地址，单位：字节地址。 */
    uint32_t payload_written;                                 /**< 已写入载荷长度，单位：字节。 */
    uint8_t transfer_enabled;                                 /**< 传输使能标志。 */
} ymodem_process_context_t;

static uint32_t ymodem_process_port_get_tick_ms(void *user);
static int ymodem_process_port_read_byte(uint8_t *byte, void *user);
static int ymodem_process_port_write_bytes(const uint8_t *data, uint32_t len, void *user);

static ymodem_core_t s_ymodem_core = {0};
static ymodem_process_context_t s_ymodem_process_ctx = {0};

static const ymodem_core_port_t s_ymodem_process_port =
{
    .read_byte = ymodem_process_port_read_byte,
    .write_bytes = ymodem_process_port_write_bytes,
    .get_tick_ms = ymodem_process_port_get_tick_ms,
    .user = &s_ymodem_process_ctx,
};

/**
 * @brief 获取 App 写入区结束地址。
 * @return uint32_t App 写入区结束地址，单位：字节地址。
 */
static uint32_t ymodem_process_get_app_end_address(void)
{
    return YMODEM_PROCESS_APP_ADDRESS + YMODEM_PROCESS_APP_SIZE;
}

/**
 * @brief 将地址向下对齐到 Flash 页边界。
 * @param address 输入地址，单位：字节地址。
 * @return uint32_t 页对齐地址，单位：字节地址。
 */
static uint32_t ymodem_process_align_down_page(uint32_t address)
{
    return address & ~(YMODEM_PROCESS_FLASH_PAGE_SIZE - 1u);
}

/**
 * @brief 将地址向上对齐到 Flash 页边界。
 * @param address 输入地址，单位：字节地址。
 * @return uint32_t 页对齐地址，单位：字节地址。
 */
static uint32_t ymodem_process_align_up_page(uint32_t address)
{
    return (address + YMODEM_PROCESS_FLASH_PAGE_SIZE - 1u) & ~(YMODEM_PROCESS_FLASH_PAGE_SIZE - 1u);
}

/**
 * @brief 擦除 App Flash 区域。
 * @param image_size 需要容纳的镜像大小，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_process_flash_erase(uint32_t image_size)
{
    uint32_t erase_address;
    uint32_t erase_end;
    uint32_t page_error = 0u;
    FLASH_EraseInitTypeDef erase = {0};

    if ((image_size == 0u) || (image_size > YMODEM_PROCESS_APP_SIZE))
    {
        return BOOT_ERR_FULL;
    }

    erase_address = ymodem_process_align_down_page(YMODEM_PROCESS_APP_ADDRESS);
    erase_end = ymodem_process_align_up_page(YMODEM_PROCESS_APP_ADDRESS + image_size);
    if (erase_end > ymodem_process_get_app_end_address())
    {
        return BOOT_ERR_FULL;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = erase_address;
    erase.NbPages = (erase_end - erase_address) / YMODEM_PROCESS_FLASH_PAGE_SIZE;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return BOOT_ERR_STATE;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return BOOT_ERR;
    }

    HAL_FLASH_Lock();
    return BOOT_OK;
}

/**
 * @brief 向 App Flash 区写入一段字节数据。
 * @param flash_address 当前写入地址指针，成功后前移到下一个写入地址。
 * @param data 待写入数据缓冲区。
 * @param data_length 待写入长度，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_process_flash_write(uint32_t *flash_address, const uint8_t *data, uint32_t data_length)
{
    uint32_t address;
    uint32_t offset = 0u;
    uint32_t app_end_address;

    if ((flash_address == NULL) || ((data == NULL) && (data_length != 0u)))
    {
        return BOOT_ERR_PARAM;
    }

    if (data_length == 0u)
    {
        return BOOT_OK;
    }

    address = *flash_address;
    app_end_address = ymodem_process_get_app_end_address();
    if ((address < YMODEM_PROCESS_APP_ADDRESS) ||
        (address > app_end_address) ||
        (data_length > (app_end_address - address)))
    {
        return BOOT_ERR_FULL;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return BOOT_ERR_STATE;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);

    while (offset < data_length)
    {
        uint32_t chunk = data_length - offset;
        uint32_t word = 0xFFFFFFFFu;

        if (chunk > 4u)
        {
            chunk = 4u;
        }

        memcpy(&word, &data[offset], chunk);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return BOOT_ERR;
        }

        if (memcmp((const void *)address, &data[offset], chunk) != 0)
        {
            HAL_FLASH_Lock();
            return BOOT_ERR_VERIFY;
        }

        address += 4u;
        offset += chunk;
    }

    HAL_FLASH_Lock();
    *flash_address = address;
    return BOOT_OK;
}

/**
 * @brief 获取当前毫秒计时。
 * @param user 端口用户上下文。
 * @return uint32_t 当前毫秒计数，单位：毫秒。
 */
static uint32_t ymodem_process_port_get_tick_ms(void *user)
{
    (void)user;
    return HAL_GetTick();
}

/**
 * @brief 从接收队列读取一个字节。
 * @param byte 字节输出缓冲区。
 * @param user 端口用户上下文。
 * @return int 成功返回 BOOT_OK，队列空返回 BOOT_ERR_NOT_FOUND。
 */
static int ymodem_process_port_read_byte(uint8_t *byte, void *user)
{
    ymodem_process_context_t *ctx;

    ctx = (ymodem_process_context_t *)user;
    if ((ctx == NULL) || (byte == NULL))
    {
        return BOOT_ERR_PARAM;
    }

    if (BootQueue_Pop(&ctx->rx_queue, byte, 1u) == 1u)
    {
        return BOOT_OK;
    }

    return BOOT_ERR_NOT_FOUND;
}

/**
 * @brief 发送 YMODEM 响应字节。
 * @param data 输出数据缓冲区。
 * @param len 输出数据长度，单位：字节。
 * @param user 端口用户上下文。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_process_port_write_bytes(const uint8_t *data, uint32_t len, void *user)
{
    (void)user;

    return can_protocol_send_ota_response(data, len);
}

/**
 * @brief 处理 YMODEM 文件头事件。
 * @param event 协议事件。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_process_handle_header_event(const ymodem_core_event_t *event)
{
    uint32_t payload_size;
    int ret;

    if ((event == NULL) || (event->file_name == NULL))
    {
        return BOOT_ERR_PARAM;
    }

    if (event->file_size < YMODEM_PROCESS_SIZE_FIRST_HEAD_PACKET)
    {
        return BOOT_ERR_STATE;
    }

    payload_size = event->file_size - YMODEM_PROCESS_SIZE_FIRST_HEAD_PACKET;
    if ((payload_size == 0u) || (payload_size > YMODEM_PROCESS_APP_SIZE))
    {
        return BOOT_ERR_FULL;
    }

    memset(&s_ymodem_process_ctx.file_info, 0, sizeof(s_ymodem_process_ctx.file_info));
    strncpy((char *)s_ymodem_process_ctx.file_info.file_name,
            event->file_name,
            sizeof(s_ymodem_process_ctx.file_info.file_name) - 1u);
    s_ymodem_process_ctx.file_info.file_size = (int32_t)event->file_size;
    s_ymodem_process_ctx.file_info.payload_size = payload_size;

    ret = ymodem_process_flash_erase(payload_size);
    if (ret != BOOT_OK)
    {
        return ret;
    }

    s_ymodem_process_ctx.flash_destination = YMODEM_PROCESS_APP_ADDRESS;
    s_ymodem_process_ctx.payload_written = 0u;
    printf("YMODEM header: file=%s total=%lu payload=%lu\r\n",
           (char *)s_ymodem_process_ctx.file_info.file_name,
           (unsigned long)event->file_size,
           (unsigned long)payload_size);

    return BOOT_OK;
}

/**
 * @brief 处理 YMODEM 数据事件。
 * @param event 协议事件。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_process_handle_data_event(const ymodem_core_event_t *event)
{
    const uint8_t *write_ptr;
    uint32_t write_length;
    uint32_t remain_length;
    int ret;

    if ((event == NULL) || (event->data == NULL))
    {
        return BOOT_ERR_PARAM;
    }

    write_ptr = event->data;
    write_length = event->data_length;

    if (event->packet_index == 1u)
    {
        if (event->data_length < YMODEM_PROCESS_SIZE_FIRST_HEAD_PACKET)
        {
            return BOOT_ERR_STATE;
        }

        memcpy(s_ymodem_process_ctx.file_info.head_packet_info,
               event->data,
               YMODEM_PROCESS_HEAD_PACKET_INFO_LENGTH);
        write_ptr = &event->data[YMODEM_PROCESS_SIZE_FIRST_HEAD_PACKET];
        write_length = event->data_length - YMODEM_PROCESS_SIZE_FIRST_HEAD_PACKET;
    }

    remain_length = s_ymodem_process_ctx.file_info.payload_size - s_ymodem_process_ctx.payload_written;
    if (write_length > remain_length)
    {
        write_length = remain_length;
    }

    ret = ymodem_process_flash_write(&s_ymodem_process_ctx.flash_destination, write_ptr, write_length);
    if (ret != BOOT_OK)
    {
        return ret;
    }

    s_ymodem_process_ctx.payload_written += write_length;
    printf("YMODEM packet: index=%u write=%lu progress=%lu/%lu\r\n",
           (unsigned int)event->packet_index,
           (unsigned long)write_length,
           (unsigned long)s_ymodem_process_ctx.payload_written,
           (unsigned long)s_ymodem_process_ctx.file_info.payload_size);

    return BOOT_OK;
}

/**
 * @brief 统一处理 YMODEM core 事件。
 * @param event 协议事件。
 * @param user 事件用户上下文。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
static int ymodem_process_handle_core_event(const ymodem_core_event_t *event, void *user)
{
    int ret;

    (void)user;

    if (event == NULL)
    {
        return BOOT_ERR_PARAM;
    }

    switch (event->type)
    {
        case YMODEM_CORE_EVENT_HEADER:
        {
            ret = ymodem_process_handle_header_event(event);
            if (ret != BOOT_OK)
            {
                s_ymodem_process_ctx.transfer_enabled = 0u;
            }

            return ret;
        }

        case YMODEM_CORE_EVENT_DATA:
        {
            ret = ymodem_process_handle_data_event(event);
            if (ret != BOOT_OK)
            {
                s_ymodem_process_ctx.transfer_enabled = 0u;
            }

            return ret;
        }

        case YMODEM_CORE_EVENT_FINISH:
        {
            s_ymodem_process_ctx.transfer_enabled = 0u;
            if (s_ymodem_process_ctx.payload_written != s_ymodem_process_ctx.file_info.payload_size)
            {
                return BOOT_ERR_VERIFY;
            }

            printf("YMODEM finish: payload=%lu\r\n", (unsigned long)s_ymodem_process_ctx.payload_written);
            return BOOT_OK;
        }

        case YMODEM_CORE_EVENT_ABORT:
        case YMODEM_CORE_EVENT_ERROR:
        {
            s_ymodem_process_ctx.transfer_enabled = 0u;
            printf("YMODEM stop: event=%d ret=%d\r\n", (int)event->type, event->result);
            return BOOT_OK;
        }

        case YMODEM_CORE_EVENT_NONE:
        default:
        {
            return BOOT_ERR_BUSY;
        }
    }
}

/**
 * @brief 初始化 YMODEM 处理模块。
 * @return void
 */
void YmodemProcess_Init(void)
{
    memset(&s_ymodem_process_ctx, 0, sizeof(s_ymodem_process_ctx));
    BootQueue_Init(&s_ymodem_process_ctx.rx_queue,
                   &s_ymodem_process_ctx.rx_buffer[0],
                   (uint16_t)sizeof(s_ymodem_process_ctx.rx_buffer));
    YmodemProcess_ResetSession();
}

/**
 * @brief 重置当前 YMODEM 会话。
 * @return void
 */
void YmodemProcess_ResetSession(void)
{
    BootQueue_Clear(&s_ymodem_process_ctx.rx_queue);
    memset(&s_ymodem_process_ctx.file_info, 0, sizeof(s_ymodem_process_ctx.file_info));
    s_ymodem_process_ctx.flash_destination = YMODEM_PROCESS_APP_ADDRESS;
    s_ymodem_process_ctx.payload_written = 0u;
    s_ymodem_process_ctx.transfer_enabled = 0u;
}

/**
 * @brief 启动一次新的 YMODEM 接收会话。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemProcess_StartSession(void)
{
    int ret;

    YmodemProcess_ResetSession();
    ret = YmodemCore_Init(&s_ymodem_core, &s_ymodem_process_port);
    if (ret != BOOT_OK)
    {
        return ret;
    }

    ret = YmodemCore_RegisterEventHandler(&s_ymodem_core,
                                          ymodem_process_handle_core_event,
                                          &s_ymodem_process_ctx);
    if (ret != BOOT_OK)
    {
        return ret;
    }

    ret = YmodemCore_Start(&s_ymodem_core);
    if (ret != BOOT_OK)
    {
        return ret;
    }

    s_ymodem_process_ctx.transfer_enabled = 1u;
    printf("YMODEM start: app=0x%08lX size=%lu\r\n",
           (unsigned long)YMODEM_PROCESS_APP_ADDRESS,
           (unsigned long)YMODEM_PROCESS_APP_SIZE);
    return BOOT_OK;
}

/**
 * @brief 轮询推进 YMODEM 接收会话。
 * @return int 成功结束返回 BOOT_OK，进行中返回 BOOT_ERR_BUSY，失败返回 BOOT_ERR_xxx。
 */
int YmodemProcess_PollSession(void)
{
    int ret;
    ymodem_core_event_t event;

    ret = YmodemCore_Poll(&s_ymodem_core, &event);
    if (ret == BOOT_ERR_BUSY)
    {
        return BOOT_ERR_BUSY;
    }

    if (ret != BOOT_OK)
    {
        s_ymodem_process_ctx.transfer_enabled = 0u;
    }

    return ret;
}

/**
 * @brief 查询当前是否处于 YMODEM 传输阶段。
 * @return uint8_t 非 0 表示传输中，0 表示未传输。
 */
uint8_t YmodemProcess_IsTransferEnabled(void)
{
    return s_ymodem_process_ctx.transfer_enabled;
}

/**
 * @brief 向 YMODEM 接收缓冲区压入一段字节流。
 * @param data 输入数据缓冲区。
 * @param len 输入数据长度，单位：字节。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemProcess_PushRxBytes(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0u))
    {
        return BOOT_ERR_PARAM;
    }

    if (BootQueue_Push(&s_ymodem_process_ctx.rx_queue, data, len) != len)
    {
        return BOOT_ERR_FULL;
    }

    return BOOT_OK;
}

/**
 * @brief 获取当前会话解析出的文件信息。
 * @param file_info 文件信息输出缓冲区。
 * @return int 成功返回 BOOT_OK，失败返回 BOOT_ERR_xxx。
 */
int YmodemProcess_GetFileInfo(ymodem_process_file_info_t *file_info)
{
    if (file_info == NULL)
    {
        return BOOT_ERR_PARAM;
    }

    *file_info = s_ymodem_process_ctx.file_info;
    return BOOT_OK;
}
