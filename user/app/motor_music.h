#ifndef MOTOR_MUSIC_H
#define MOTOR_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MOTOR_MUSIC_POWER_ON_ENABLE          1u
#define MOTOR_MUSIC_DEFAULT_TICK_HZ          4000u
#define MOTOR_MUSIC_DEFAULT_AMPLITUDE_PERCENT 50u

/**
 * @brief 电机音乐状态机状态。
 */
typedef enum
{
    MOTOR_MUSIC_STATE_IDLE = 0, /**< 空闲状态，单位：无。 */
    MOTOR_MUSIC_STATE_PLAYING,  /**< 播放状态，单位：无。 */
} motor_music_state_t;

/**
 * @brief 电机音乐音符。
 */
typedef struct
{
    uint16_t frequency_hz; /**< 音符频率，单位：Hz，0 表示休止符。 */
    uint16_t duration_ms;  /**< 音符时长，单位：毫秒。 */
} motor_music_note_t;

/**
 * @brief 初始化电机音乐状态机。
 * @return void
 */
void MotorMusic_Init(void);

/**
 * @brief 启动上电音乐。
 * @return void
 */
void MotorMusic_StartPowerOn(void);

/**
 * @brief 停止当前音乐播放。
 * @return void
 */
void MotorMusic_Stop(void);

/**
 * @brief 推进电机音乐状态机。
 * @param tick_hz 调用频率，单位：Hz。
 * @return int32_t 当前 q 轴音乐目标值，单位：内部控制量。
 */
int32_t MotorMusic_Poll(uint16_t tick_hz);

/**
 * @brief 判断音乐是否正在播放。
 * @return uint8_t 正在播放返回 1，否则返回 0。
 */
uint8_t MotorMusic_IsPlaying(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_MUSIC_H */
