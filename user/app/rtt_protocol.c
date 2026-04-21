#include "rtt_protocol.h"
#include "stm32f1xx_it.h"
#include "can.h"
#include "log.h"
#include "flash.h"
#include "foc_init.h"

typedef enum {
    CMD_IDLE,
    CMD_ANGLE_RESET,
    CMD_CAN_SET,
    CMD_CAN_GET,
    CMD_Q_SET,
    CMD_SPEED_PID_PARAM,
    CMD_SPEED_PID_TARGET,
    CMD_SPEED_PID_GET_PARAM,
    CMD_SPEED_PID_GET_TARGET,
    CMD_UNKNOWN
} PARSED_COMMAND;

#define RX_BUFFER_SIZE 64
static char rx_buffer[RX_BUFFER_SIZE];
static int rx_index = 0;

// 2. 命令解析函数（核心）
static PARSED_COMMAND parse_command(const char* cmd_line) {
    char line[RX_BUFFER_SIZE];
    strncpy(line, cmd_line, RX_BUFFER_SIZE);
    line[RX_BUFFER_SIZE-1] = '\0';
    
    int len = strlen(line);
    while(len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }
    
    if (strcmp(line, "angle reset") == 0) {
        return CMD_ANGLE_RESET;
    }
    
    if (strstr(line, "can set") == line) {
        return CMD_CAN_SET;
    }

    if (strstr(line, "can get") == line) {
        return CMD_CAN_GET;
    }
    
    if (strstr(line, "q set") == line) {
        return CMD_Q_SET;
    }
    
    // 检查是否是"speed pid param set"命令
    if (strstr(line, "speed pid param set") == line) {
        return CMD_SPEED_PID_PARAM;
    }
    
    // 检查是否是"speed pid target set"命令
    if (strstr(line, "speed pid target set") == line) {
        return CMD_SPEED_PID_TARGET;
    }
    
    // 检查是否是"speed pid param get"命令
    if (strstr(line, "speed pid param get") == line) {
        return CMD_SPEED_PID_GET_PARAM;
    }
    
    // 检查是否是"speed pid target get"命令
    if (strstr(line, "speed pid target get") == line) {
        return CMD_SPEED_PID_GET_TARGET;
    }
    
    return CMD_UNKNOWN;
}

// 3. 辅助函数
static int parse_percentage_to_q15(const char* str, int16_t *q15_value) {
    char *endptr;
    char temp_str[32];
    strncpy(temp_str, str, sizeof(temp_str)-1);
    temp_str[sizeof(temp_str)-1] = '\0';
    
    int has_percent = 0;
    int len = strlen(temp_str);
    if (len > 0 && temp_str[len-1] == '%') {
        has_percent = 1;
        temp_str[len-1] = '\0';
    }
    
    if (strstr(temp_str, "0x") == temp_str || strstr(temp_str, "0X") == temp_str) {
        long val = strtol(temp_str, &endptr, 16);
        if (*endptr != '\0' && *endptr != ' ') {
            return -1;
        }
        if (val < -32768 || val > 32767) {
            return -1;
        }
        *q15_value = (int16_t)val;
        return 0;
    }
    
    float float_val = strtof(temp_str, &endptr);
    if (*endptr != '\0' && *endptr != ' ') {
        return -1;
    }
    
    if (has_percent) {
        if (float_val < -100.0f || float_val > 100.0f) {
            return -1;
        }
        
        float scaled = float_val * 327.68f;
        if (scaled >= 0) {
            *q15_value = (int16_t)(scaled + 0.5f);
        } else {
            *q15_value = (int16_t)(scaled - 0.5f);
        }
        
        if (*q15_value > 32767) *q15_value = 32767;
        if (*q15_value < -32768) *q15_value = -32768;
        
        return 0;
    } else {
        if (float_val < -1.0f || float_val >= 1.0f) {
            return -1;
        }
        
        float scaled = float_val * 32768.0f;
        if (scaled >= 0) {
            *q15_value = (int16_t)(scaled + 0.5f);
        } else {
            *q15_value = (int16_t)(scaled - 0.5f);
        }
        
        if (*q15_value > 32767) *q15_value = 32767;
        if (*q15_value < -32768) *q15_value = -32768;
        
        return 0;
    }
}

static int parse_angle_to_rad_x1000(const char* angle_str, int32_t *result) {
    char *endptr;
    float angle_deg = strtof(angle_str, &endptr);
    
    if (*endptr != '\0' && *endptr != ' ' && *endptr != '\r' && *endptr != '\n') {
        return -1;
    }
    
    const float pi = 3.14159265358979323846f;
    float rad = angle_deg * pi / 180.0f;
    float rad_x1000 = rad * 1000.0f;
    
    if (rad_x1000 >= 0) {
        *result = (int32_t)(rad_x1000 + 0.5f);
    } else {
        *result = (int32_t)(rad_x1000 - 0.5f);
    }
    
    return 0;
}

static int parse_out_max_value(const char* str, float *value, const char* param_name) {
    char *endptr;
    float val = strtof(str, &endptr);
    
    if (*endptr != '\0' && *endptr != ' ' && *endptr != '\r' && *endptr != '\n') {
        return -1;
    }
    
    if (val < 0 || val > OUT_MAX) {
        printf("Error: %s out of range (0-%d)\n", param_name, OUT_MAX);
        return -1;
    }
    
    *value = val;
    return 0;
}

// 4. 命令执行函数
/**
 * @brief 执行解析后的 RTT 命令。
 * @param cmd 已解析的命令类型。
 * @param original_line 原始命令字符串。
 * @return void
 */
static void execute_command(PARSED_COMMAND cmd, const char* original_line) {
    switch(cmd) {
        case CMD_ANGLE_RESET:
            printf("RTT CLI: Executing angle reset...\n");
            recoder_data data = {0};
            (void)Load_Recoder(&data);
            data.calibration_angle = foc_zero_angle_reset();
            Save_Recoder(data);
            printf("RTT CLI: Angle reset completed, angle: %d.\n", data.calibration_angle);
            break;
            
        case CMD_CAN_SET: {
            printf("RTT CLI: Parsing CAN ID set command...\n");
            char *p = (char*)original_line;
            p += 7; // Skip "can set"
            while(*p == ' ') p++;
            
            if (strncmp(p, "0x", 2) == 0 || strncmp(p, "0X", 2) == 0) {
                unsigned int can_id = (unsigned int)strtoul(p, NULL, 16);
                printf("RTT CLI: Setting CAN ID to 0x%X (%u)\n", can_id, can_id);
                recoder_data data = {0};
                (void)Load_Recoder(&data);
                data.can_id = can_id;
                Save_Recoder(data);
                printf("RTT CLI: CAN ID set completed.\n");
            } else {
                printf("RTT CLI: Error! CAN ID format should be 0xXXX (hexadecimal).\n");
            }
            break;
        }

        case CMD_CAN_GET:
        {
            recoder_data data = {0};
            (void)Load_Recoder(&data);
            printf("RTT CLI: CAN ID: 0x%X (%u)\n", data.can_id, data.can_id);
            break;
        }
        
        case CMD_Q_SET: {
            printf("RTT CLI: Parsing Q15 set command...\n");
            char *param_start = strstr(original_line, "q set");
            if (param_start == NULL) {
                printf("RTT CLI: Command format error.\n");
                break;
            }
            
            param_start += 5;
            while (*param_start == ' ') param_start++;
            
            if (*param_start == '\0') {
                printf("RTT CLI: Error! Need to provide Q15 value. Format: q set xxx\n");
                printf("  Example: q set 50%%       (percentage)\n");
                printf("           q set -75%%      (negative percentage)\n");
                printf("           q set 0.5       (decimal, means 0.5%%)\n");
                printf("           q set 0x4000    (hex Q15 value)\n");
                break;
            }
            
            int16_t q15_val;
            if (parse_percentage_to_q15(param_start, &q15_val) == 0) {
                float percent_val = (float)q15_val / 327.68f;
                float float_val = (float)q15_val / 32768.0f;
                
                printf("RTT CLI: Q15 value set successfully.\n");
                printf("  Hex: 0x%04X\n", (uint16_t)q15_val);
                printf("  Decimal integer: %d\n", q15_val);
                printf("  Corresponding percentage: %.2f%%\n", percent_val);
                printf("  Corresponding float: %f\n", float_val);
                
                foc_set_target(0,q15_val,0);
            } else {
                printf("RTT CLI: Value parse failed!\n");
                printf("  Valid formats:\n");
                printf("    Percentage: -100%% to 100%% (e.g., q set 50%%)\n");
                printf("    Decimal: -1.0 to 0.999969 (e.g., q set 0.5)\n");
                printf("    Hexadecimal: 0x8000 to 0x7FFF (e.g., q set 0x4000)\n");
            }
            break;
        }
            
        case CMD_SPEED_PID_PARAM: {
            printf("RTT CLI: Parsing speed PID parameter set command...\n");
            
            char params_copy[128];
            strncpy(params_copy, original_line, sizeof(params_copy)-1);
            params_copy[sizeof(params_copy)-1] = '\0';
            
            // 命令格式: speed pid param set P I I_out_max Out_max
            char *token = strtok(params_copy, " ");
            if (token == NULL || strcmp(token, "speed") != 0) {
                printf("RTT CLI: Command format error.\n");
                break;
            }
            
            // 跳过 "pid"
            token = strtok(NULL, " ");
            if (token == NULL || strcmp(token, "pid") != 0) {
                printf("RTT CLI: Command format error.\n");
                break;
            }
            
            // 跳过 "param"
            token = strtok(NULL, " ");
            if (token == NULL || strcmp(token, "param") != 0) {
                printf("RTT CLI: Command format error.\n");
                break;
            }
            
            // 跳过 "set"
            token = strtok(NULL, " ");
            if (token == NULL || strcmp(token, "set") != 0) {
                printf("RTT CLI: Command format error.\n");
                break;
            }
            
            // 解析 P 参数
            token = strtok(NULL, " ");
            if (token == NULL) {
                printf("RTT CLI: Missing P parameter. Format: speed pid param set P I I_out_max Out_max\n");
                break;
            }
            float p = strtof(token, NULL);
            
            // 解析 I 参数
            token = strtok(NULL, " ");
            if (token == NULL) {
                printf("RTT CLI: Missing I parameter. Format: speed pid param set P I I_out_max Out_max\n");
                break;
            }
            float i = strtof(token, NULL);
            
            // 解析 I_out_max 参数
            token = strtok(NULL, " ");
            if (token == NULL) {
                printf("RTT CLI: Missing I_out_max parameter. Format: speed pid param set P I I_out_max Out_max\n");
                break;
            }
            float i_out_max;
            if (parse_out_max_value(token, &i_out_max, "I_out_max") != 0) {
                break;
            }
            
            // 解析 Out_max 参数
            token = strtok(NULL, " ");
            if (token == NULL) {
                printf("RTT CLI: Missing Out_max parameter. Format: speed pid param set P I I_out_max Out_max\n");
                break;
            }
            float out_max;
            if (parse_out_max_value(token, &out_max, "Out_max") != 0) {
                break;
            }
            
            printf("RTT CLI: Setting speed PID parameters:\n");
            // 将浮点数转换为整数输出（乘以1000保留3位小数）
            printf("  P = %d (x1000)\n", (int)(p * 1000));
            printf("  I = %d (x1000)\n", (int)(i * 1000));
            printf("  I_out_max = %d (0x%04X)\n", (int)i_out_max, (uint16_t)i_out_max);
            printf("  Out_max = %d (0x%04X)\n", (int)out_max, (uint16_t)out_max);
            
            foc_speed_pid_set_param(p, i, i_out_max, out_max);
            printf("RTT CLI: Speed PID parameters set completed.\n");
            break;
        }
        
        case CMD_SPEED_PID_TARGET: {
            printf("RTT CLI: Parsing speed PID target set command...\n");
            
            char params_copy[128];
            strncpy(params_copy, original_line, sizeof(params_copy)-1);
            params_copy[sizeof(params_copy)-1] = '\0';
            
            char *token;
            
            // 跳过 "speed"
            token = strtok(params_copy, " ");
            if (token == NULL || strcmp(token, "speed") != 0) {
                printf("RTT CLI: Command format error.\n");
                break;
            }
            
            // 跳过 "pid"
            token = strtok(NULL, " ");
            if (token == NULL || strcmp(token, "pid") != 0) {
                printf("RTT CLI: Command format error.\n");
                break;
            }
            
            // 跳过 "target"
            token = strtok(NULL, " ");
            if (token == NULL || strcmp(token, "target") != 0) {
                printf("RTT CLI: Command format error.\n");
                break;
            }
            
            // 跳过 "set"
            token = strtok(NULL, " ");
            if (token == NULL || strcmp(token, "set") != 0) {
                printf("RTT CLI: Command format error.\n");
                break;
            }
            
            // 解析rad/s参数
            token = strtok(NULL, " ");
            if (token == NULL) {
                printf("RTT CLI: Missing rad/s parameter. Format: speed pid target set rad_s_value\n");
                printf("  Example: speed pid target set 6283  # 6283 rad/s\n");
                break;
            }
            
            char *endptr;
            // 解析整数值
            long target = strtol(token, &endptr, 10);
            
            if (*endptr != '\0' && *endptr != ' ' && *endptr != '\r' && *endptr != '\n') {
                printf("RTT CLI: Invalid integer value!\n");
                printf("  Format: speed pid target set rad_s_value\n");
                printf("  Example: speed pid target set 6283  # 6283 rad/s\n");
                break;
            }
            
            // 检查是否溢出
            if (target < INT32_MIN || target > INT32_MAX) {
                printf("RTT CLI: Value out of range! Must be between %d and %d\n", INT32_MIN, INT32_MAX);
                break;
            }
            
            printf("RTT CLI: Setting speed target:\n");
            printf("  Target value: %ld rad/s\n", target);
            
            foc_speed_pid_set_target((float)target);
            printf("RTT CLI: Speed target set completed.\n");
            break;
        }
        
        case CMD_SPEED_PID_GET_TARGET: {
            printf("RTT CLI: Reading current speed PID target...\n");
            
            float target;
            foc_speed_pid_get_target(&target);
            
            printf("Current speed PID target:\n");
            printf("  Value: %d rad/s(x1000)\n", (int32_t)(target*1000));
            break;
        }
        
        case CMD_SPEED_PID_GET_PARAM: {
            printf("RTT CLI: Reading current speed PID parameters...\n");
            
            float p, i, i_out_max, out_max;
            foc_speed_pid_get_param(&p, &i, &i_out_max, &out_max);
            
            printf("Current speed PID parameters:\n");
            printf("  P = %d (x1000)\n", (int)(p * 1000));
            printf("  I = %d (x1000)\n", (int)(i * 1000));
            printf("  I_out_max = %d (0x%04X)\n", (int)i_out_max, (uint16_t)i_out_max);
            printf("  Out_max = %d (0x%04X)\n", (int)out_max, (uint16_t)out_max);
            printf("  Valid range: 0 - %d (0x%04X)\n", OUT_MAX, OUT_MAX);
            break;
        }
            
        case CMD_UNKNOWN:
            printf("RTT CLI: Unknown command. Supported commands:\n");
            printf("  - angle reset\n");
            printf("  - can set 0xXXX\n");
            printf("  - q set xxx (percentage, decimal or hex)\n");
            printf("  - speed pid param set P I I_out_max Out_max\n");
            printf("  - speed pid target set angle\n");
            printf("  - speed pid param get\n");
            printf("  - speed pid target get\n");
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
            
            printf("RTT CLI: 警告：接收缓冲区溢出，清空缓冲区。\n");
            rx_index = 0;
            rx_buffer[0] = '\0';
        }
    }
}
