#include "foc_angle.h"

// 初始化角度估计器
void AngleEstimator_Init(AngleEstimator* estimator, AngleEstimatorConfig* config) {
    if (!estimator || !config) return;
    
    // 复制配置参数
    memcpy(&estimator->config, config, sizeof(AngleEstimatorConfig));
    
    // 分配缓冲区内存
    estimator->time_buffer = (float*)malloc(config->window_size * sizeof(float));
    estimator->angle_buffer = (float*)malloc(config->window_size * sizeof(float));
    
    if (!estimator->time_buffer || !estimator->angle_buffer) {
        // 内存分配失败，使用静态缓冲区或报错
        return;
    }
    
    // 初始化状态变量
    memset(estimator->time_buffer, 0, config->window_size * sizeof(float));
    memset(estimator->angle_buffer, 0, config->window_size * sizeof(float));
    
    estimator->angle_raw = 0.0f;
    estimator->angle_unwrapped = 0.0f;
    estimator->angle_filtered = 0.0f;
    estimator->angular_velocity = 0.0f;
    estimator->angular_accel = 0.0f;
    estimator->angle_offset = 0.0f;
    estimator->rotation_count = 0;
    
    estimator->buffer_index = 0;
    estimator->buffer_count = 0;
    
    estimator->sum_t = 0.0f;
    estimator->sum_t2 = 0.0f;
    estimator->sum_a = 0.0f;
    estimator->sum_ta = 0.0f;
    
    estimator->is_initialized = 0;
    estimator->last_angle_raw = 0.0f;
}

// 释放角度估计器资源
void AngleEstimator_Deinit(AngleEstimator* estimator) {
    if (!estimator) return;
    
    if (estimator->time_buffer) {
        free(estimator->time_buffer);
        estimator->time_buffer = NULL;
    }
    
    if (estimator->angle_buffer) {
        free(estimator->angle_buffer);
        estimator->angle_buffer = NULL;
    }
}

// 角度展开处理（处理0-2π跳变）
static float AngleUnwrap(AngleEstimator* estimator, float current_angle) {
    float angle_diff = current_angle - estimator->last_angle_raw;
    
    // 检测角度突变（超过阈值认为是跨周期）
    if (angle_diff > estimator->config.angle_threshold) {
        // 逆时针跨过0点，说明转了一圈
        estimator->rotation_count--;
        estimator->angle_offset -= 2.0f * (float)M_PI;
    } else if (angle_diff < -estimator->config.angle_threshold) {
        // 顺时针跨过0点，说明转了一圈
        estimator->rotation_count++;
        estimator->angle_offset += 2.0f * (float)M_PI;
    }
    
    // 更新上一次角度
    estimator->last_angle_raw = current_angle;
    
    // 返回展开后的角度
    return current_angle + estimator->angle_offset;
}

// 更新最小二乘统计量
static void UpdateLeastSquaresStats(AngleEstimator* estimator, float t, float angle) {
    if (estimator->buffer_count < estimator->config.window_size) {
        // 缓冲区未满，直接添加
        estimator->sum_t += t;
        estimator->sum_t2 += t * t;
        estimator->sum_a += angle;
        estimator->sum_ta += t * angle;
        estimator->buffer_count++;
    } else {
        // 缓冲区已满，替换最旧的数据
        float old_t = estimator->time_buffer[estimator->buffer_index];
        float old_angle = estimator->angle_buffer[estimator->buffer_index];
        
        estimator->sum_t += t - old_t;
        estimator->sum_t2 += t * t - old_t * old_t;
        estimator->sum_a += angle - old_angle;
        estimator->sum_ta += t * angle - old_t * old_angle;
    }
    
    // 存储到缓冲区
    estimator->time_buffer[estimator->buffer_index] = t;
    estimator->angle_buffer[estimator->buffer_index] = angle;
    
    // 更新缓冲区索引
    estimator->buffer_index = (estimator->buffer_index + 1) % estimator->config.window_size;
}

// 执行最小二乘拟合，计算角速度和加速度
static void PerformLeastSquares(AngleEstimator* estimator, float* velocity, float* acceleration) {
    if (estimator->buffer_count < 3) {
        // 数据不足，无法进行拟合
        *velocity = 0.0f;
        *acceleration = 0.0f;
        return;
    }
    
    float n = (float)estimator->buffer_count;
    float det = n * estimator->sum_t2 - estimator->sum_t * estimator->sum_t;
    
    if (fabsf(det) < 1e-6f) {
        // 行列式接近0，无法求解
        *velocity = 0.0f;
        *acceleration = 0.0f;
        return;
    }
    
    // 使用最小二乘求解角度 = a0 + a1*t + a2*t² 的系数
    // 但为了简化，我们使用线性拟合：角度 = a + b*t
    float b = (n * estimator->sum_ta - estimator->sum_t * estimator->sum_a) / det;
    float a = (estimator->sum_a - b * estimator->sum_t) / n;
    
    // 角速度是b（弧度/时间单位）
    *velocity = b;
    
    // 为了计算加速度，我们需要二次拟合，这里简化使用最近几个点的速度差分
    if (estimator->buffer_count >= 5) {
        int idx1 = (estimator->buffer_index - 1 + estimator->config.window_size) % estimator->config.window_size;
        int idx2 = (estimator->buffer_index - 2 + estimator->config.window_size) % estimator->config.window_size;
        
        if (estimator->time_buffer[idx1] != estimator->time_buffer[idx2]) {
            float vel1 = (estimator->angle_buffer[idx1] - estimator->angle_buffer[idx2]) / 
                        (estimator->time_buffer[idx1] - estimator->time_buffer[idx2]);
            *acceleration = (b - vel1) / (estimator->config.Ts * 5.0f);  // 使用5个周期的时间差
        } else {
            *acceleration = 0.0f;
        }
    } else {
        *acceleration = 0.0f;
    }
    
    // 限幅
    if (fabsf(*velocity) > estimator->config.max_angular_vel) {
        *velocity = (*velocity > 0) ? estimator->config.max_angular_vel : -estimator->config.max_angular_vel;
    }
}

// 主处理函数 - 10kHz调用
// raw_angle: AS5600读取的原始角度(0~2π)，每1ms更新一次
// current_time: 当前时间(s)，用于最小二乘拟合
// 返回: 估计的角速度(rad/s)
float AngleEstimator_Process(AngleEstimator* estimator, float raw_angle) {
    if (!estimator) return 0.0f;
    
    static float current_time = 0.0f;
    // 增加时间
    current_time += estimator->config.Ts;
    
    // 角度展开
    float unwrapped_angle = AngleUnwrap(estimator, raw_angle);
    estimator->angle_unwrapped = unwrapped_angle;
    estimator->angle_raw = raw_angle;
    
    // 更新最小二乘统计量
    UpdateLeastSquaresStats(estimator, current_time, unwrapped_angle);
    
    // 执行最小二乘拟合
    float velocity, acceleration;
    PerformLeastSquares(estimator, &velocity, &acceleration);
    
    // 应用平滑滤波
    float alpha = estimator->config.smoothing_factor;
    estimator->angular_velocity = alpha * estimator->angular_velocity + (1.0f - alpha) * velocity;
    estimator->angular_accel = alpha * estimator->angular_accel + (1.0f - alpha) * acceleration;
    
    // 使用估计的角速度预测角度（用于高频更新）
    estimator->angle_filtered = unwrapped_angle;  // 或者使用预测: unwrapped_angle + estimator->angular_velocity * estimator->config.Ts
    
    return estimator->angular_velocity;
}

// 获取展开的连续角度(rad)
float AngleEstimator_GetUnwrappedAngle(AngleEstimator* estimator) {
    return estimator ? estimator->angle_unwrapped : 0.0f;
}

// 获取滤波后角度(0~2π)
float AngleEstimator_GetFilteredAngle(AngleEstimator* estimator) {
    if (!estimator) return 0.0f;
    
    // 将展开的角度映射回0~2π
    float angle = fmodf(estimator->angle_unwrapped, 2.0f * (float)M_PI);
    if (angle < 0) angle += 2.0f * (float)M_PI;
    
    return angle;
}

// 获取估计的角速度(rad/s)
float AngleEstimator_GetAngularVelocity(AngleEstimator* estimator) {
    return estimator ? estimator->angular_velocity : 0.0f;
}

// 获取估计的角加速度(rad/s²)
float AngleEstimator_GetAngularAcceleration(AngleEstimator* estimator) {
    return estimator ? estimator->angular_accel : 0.0f;
}

// 获取旋转圈数
int32_t AngleEstimator_GetRotationCount(AngleEstimator* estimator) {
    return estimator ? estimator->rotation_count : 0;
}

// 获取总旋转角度(rad)
float AngleEstimator_GetTotalRotation(AngleEstimator* estimator) {
    return estimator ? estimator->angle_unwrapped : 0.0f;
}

// 重置估计器（例如在归零操作后）
void AngleEstimator_Reset(AngleEstimator* estimator) {
    if (!estimator) return;
    
    estimator->angle_offset = 0.0f;
    estimator->rotation_count = 0;
    estimator->angle_unwrapped = estimator->angle_raw;
    estimator->angular_velocity = 0.0f;
    estimator->angular_accel = 0.0f;
    
    // 清空缓冲区
    memset(estimator->time_buffer, 0, estimator->config.window_size * sizeof(float));
    memset(estimator->angle_buffer, 0, estimator->config.window_size * sizeof(float));
    estimator->buffer_index = 0;
    estimator->buffer_count = 0;
    
    estimator->sum_t = 0.0f;
    estimator->sum_t2 = 0.0f;
    estimator->sum_a = 0.0f;
    estimator->sum_ta = 0.0f;
}