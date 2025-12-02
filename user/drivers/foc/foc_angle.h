#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 角度估计器配置参数结构体
typedef struct {
    float Ts;                // 采样周期 (s)，100us for 10kHz
    int window_size;         // 滑动窗口大小（用于最小二乘）
    float angle_threshold;   // 角度突变检测阈值 (rad)
    float max_angular_vel;   // 最大角速度 (rad/s)
    float smoothing_factor;  // 平滑因子 (0~1)
} AngleEstimatorConfig;

// 角度估计器状态结构体
typedef struct {
    float angle_raw;          // 原始角度 (0~2π)
    float angle_unwrapped;    // 展开的连续角度 (rad)
    float angle_filtered;     // 滤波后角度 (0~2π)
    float angular_velocity;   // 角速度估计 (rad/s)
    float angular_accel;      // 角加速度估计 (rad/s²)
    float angle_offset;       // 角度偏移累积
    int32_t rotation_count;   // 旋转圈数计数
    
    // 最小二乘拟合相关
    float *time_buffer;       // 时间缓冲区
    float *angle_buffer;      // 角度缓冲区
    int buffer_index;         // 缓冲区索引
    int buffer_count;         // 缓冲区有效数据计数
    
    // 统计量
    float sum_t;              // ∑t
    float sum_t2;             // ∑t²
    float sum_a;              // ∑angle
    float sum_ta;             // ∑t*angle
    
    // 状态标志
    uint8_t is_initialized;   // 初始化标志
    float last_angle_raw;     // 上一次原始角度
    
    // 配置参数
    AngleEstimatorConfig config;
} AngleEstimator;


void AngleEstimator_Init(AngleEstimator* estimator, AngleEstimatorConfig* config);
float AngleEstimator_Process(AngleEstimator* estimator, float raw_angle);
float AngleEstimator_GetAngularVelocity(AngleEstimator* estimator);
float AngleEstimator_GetFilteredAngle(AngleEstimator* estimator);