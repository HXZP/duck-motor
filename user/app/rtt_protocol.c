#include "rtt_protocol.h"
#include "stm32f1xx_it.h"
#include "can.h"
#include "log.h"
#include "flash.h"
#include "foc_init.h"

typedef enum {
    CMD_IDLE,
    CMD_ANGLE_RESET,
    CMD_SET_Q,
    CMD_CAN_SET,
    CMD_CAN_GET, // 这里可以添加更多命令

    CMD_UNKNOWN
} PARSED_COMMAND;

#define RX_BUFFER_SIZE 64
static char rx_buffer[RX_BUFFER_SIZE];
static int rx_index = 0;

// 2. 命令解析函数（核心）
static PARSED_COMMAND parse_command(const char* cmd_line) {
    // 去除末尾换行符 (如果PC端发送时带了回车)
    char line[RX_BUFFER_SIZE];
    strncpy(line, cmd_line, RX_BUFFER_SIZE);
    line[RX_BUFFER_SIZE-1] = '\0';
    
    int len = strlen(line);
    while(len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }
    
    // 检查是否是"角度复位"或其英文命令
    if (strcasecmp(line, "angle reset") == 0) {
        return CMD_ANGLE_RESET;
    }
    
    if (strstr(line, "q set") == line) { // 确保从行首开始匹配
        return CMD_SET_Q;
    }

    // 检查是否是"can set"命令
    // 使用strstr检查是否包含"can set"前缀
    if (strstr(line, "can set") == line) { // 确保从行首开始匹配
        return CMD_CAN_SET;
    }

    if (strstr(line, "can get") == line) { // 确保从行首开始匹配
        return CMD_CAN_GET;
    }

    return CMD_UNKNOWN;
}

// 1. 修改Q15转换辅助函数以支持百分比
/**
 * @brief 将百分比字符串转换为Q15格式的16位有符号整数
 * @param str 输入字符串，可以是百分比、小数或十六进制
 * @param q15_value 输出的Q15值指针
 * @return 0成功，-1失败
 */
static int parse_percentage_to_q15(const char* str, int16_t *q15_value) {
    char *endptr;
    char temp_str[32];
    strncpy(temp_str, str, sizeof(temp_str)-1);
    temp_str[sizeof(temp_str)-1] = '\0';
    
    // 去除可能的百分号
    int has_percent = 0;
    int len = strlen(temp_str);
    if (len > 0 && temp_str[len-1] == '%') {
        has_percent = 1;
        temp_str[len-1] = '\0';
    }
    
    // 检查是否为十六进制格式
    if (strstr(temp_str, "0x") == temp_str || strstr(temp_str, "0X") == temp_str) {
        long val = strtol(temp_str, &endptr, 16);
        if (*endptr != '\0' && *endptr != ' ') {
            return -1; // 解析失败
        }
        // Q15是16位有符号，检查范围
        if (val < -32768 || val > 32767) {
            return -1;
        }
        *q15_value = (int16_t)val;
        return 0;
    }
    
    // 解析为十进制浮点数
    float float_val = strtof(temp_str, &endptr);
    if (*endptr != '\0' && *endptr != ' ') {
        return -1; // 解析失败
    }
    
    if (has_percent) {
        // 百分比模式：-100%到100% 映射到 Q15的-32768到32767
        if (float_val < -100.0f || float_val > 100.0f) {
            return -1; // 超出百分比范围
        }
        
        // 线性映射：百分比 -> Q15
        // 当百分比为100%时，映射到32767；-100%时映射到-32768
        float scaled = float_val * 327.68f; // 32767/100 ≈ 327.68
        
        // 四舍五入并饱和处理
        if (scaled >= 0) {
            *q15_value = (int16_t)(scaled + 0.5f);
        } else {
            *q15_value = (int16_t)(scaled - 0.5f);
        }
        
        // 边界处理
        if (*q15_value > 32767) *q15_value = 32767;
        if (*q15_value < -32768) *q15_value = -32768;
        
        return 0;
    } else {
        // 原来的小数模式：-1.0到1.0-2^-15
        if (float_val < -1.0f || float_val >= 1.0f) {
            return -1;
        }
        
        // 转换为Q15格式：乘以32768并四舍五入
        float scaled = float_val * 32768.0f;
        if (scaled >= 0) {
            *q15_value = (int16_t)(scaled + 0.5f);
        } else {
            *q15_value = (int16_t)(scaled - 0.5f);
        }
        
        // 边界处理：最大值32767对应0.999969482421875
        if (*q15_value > 32767) *q15_value = 32767;
        if (*q15_value < -32768) *q15_value = -32768;
        
        return 0;
    }
}
// 3. 命令执行函数
static void execute_command(PARSED_COMMAND cmd, const char* original_line) {
    switch(cmd) {
        case CMD_ANGLE_RESET:
        {
            printf("Zero angle...\n");
            recoder_data data = {0};
            Load_Recoder(&data);
            data.calibration_angle = foc_zero_angle_reset();
            Save_Recoder(data);
            printf("zero angle saved: %d\n", data.calibration_angle);
            break;
        }
        
        case CMD_SET_Q:
        {
            // 提取参数部分，跳过"q set"
            char *param_start = strstr(original_line, "q set");
            if (param_start == NULL) {
                printf("error format: %s\n", original_line);
                break;
            }
            
            param_start += 5; // 跳过"q set"
            while (*param_start == ' ') param_start++; // 跳过空格
            
            if (*param_start == '\0') {
                printf("error format: %s\n", original_line);
                break;
            }
            
            // 解析Q15值
            int16_t q15_val;
            if (parse_percentage_to_q15(param_start, &q15_val) == 0) {
                // 计算对应的百分比值用于显示
                float percent_val = (float)q15_val / 327.68f; // 32767/100 ≈ 327.68
                float float_val = (float)q15_val / 32768.0f;
                
                printf("set Q15 value success.\n");
                printf("  hex: 0x%04X\n", (uint16_t)q15_val);
                printf("  int: %d\n", q15_val);
                printf("  percent: %.2f%%\n", percent_val);
                printf("  float: %f\n", float_val);
                
                // TODO: 在这里调用你的Q15值设置函数
                foc_set_target(0,q15_val,0);
            } else {
                printf("error format: %s\n", original_line);
                printf("  valid format:\n");
                printf("    percent: -100%% to 100%% (e.g: q set 50%%)\n");
                printf("    decimal: -1.0 to 0.999969 (e.g: q set 0.5)\n");
                printf("    hex: 0x8000 to 0x7FFF (e.g: q set 0x4000)\n");
            }
            break;
        }

        case CMD_CAN_SET:
        {
            // 提取十六进制参数
            // 命令格式期望: "can set 0x123"
            const char *p = original_line;
            p += 7; // 跳过"can set" (7个字符)
            while(*p == ' ') p++; // 跳过空格
            
            if (strncmp(p, "0x", 2) == 0 || strncmp(p, "0X", 2) == 0) {
                unsigned int can_id = (unsigned int)strtoul(p, NULL, 16);
                SEGGER_RTT_printf(0, "Set CAN ID 0x%X (%u)\n", can_id, can_id);
                recoder_data data = {0};
                Load_Recoder(&data);
                data.can_id = can_id;
                Save_Recoder(data);
            } else {
                printf("Error! CAN ID format should be 0xXXX (hexadecimal).\n");
            }
            break;
        }

        case CMD_CAN_GET:
        {
            recoder_data data = {0};
            Load_Recoder(&data);
            SEGGER_RTT_printf(0, "CAN ID: 0x%X (%u)\n", data.can_id, data.can_id);
            break;
        }

        case CMD_UNKNOWN:
            printf("Unknown command. Supported commands:\n");
            printf("  - angle reset / angle reset\n");
            printf("  - can set 0xXXX\n");
            printf("  - can get\n");
            printf("  - q set xx%%\n");
            break;
            
        case CMD_IDLE:
        default:
            break;
    }
}

// 4. 主循环处理函数（需要周期性调用）
void process_rtt_commands(void) {
    // 尝试从RTT下行缓冲区读取数据
    int num_read = SEGGER_RTT_Read(0, &rx_buffer[rx_index], RX_BUFFER_SIZE - rx_index - 1);
    
    if(num_read > 0) {
        rx_index += num_read;
        rx_buffer[rx_index] = '\0'; // 确保字符串终止
        
        // 检查是否有完整的行（以换行符分隔）
        char *line_start = rx_buffer;
        char *newline_ptr;
        
        while((newline_ptr = strchr(line_start, '\n')) != NULL) {
            *newline_ptr = '\0'; // 替换换行符为字符串结束符
            
            // 解析并执行这一行命令
            PARSED_COMMAND cmd = parse_command(line_start);
            if(cmd != CMD_IDLE) {
                execute_command(cmd, line_start);
            }
            
            line_start = newline_ptr + 1; // 移动到下一行开始
        }
        
        // 将未处理完的数据移到缓冲区开头
        if(line_start > rx_buffer) {
            int remaining = rx_buffer + rx_index - line_start;
            if(remaining > 0) {
                memmove(rx_buffer, line_start, remaining);
            }
            rx_index = remaining;
            rx_buffer[rx_index] = '\0';
        }
        
        // 防止缓冲区溢出
        if(rx_index >= RX_BUFFER_SIZE - 1) {
            SEGGER_RTT_printf(0, "RTT CLI: 警告：接收缓冲区溢出，清空缓冲区。\n");
            rx_index = 0;
            rx_buffer[0] = '\0';
        }
    }
}