#include "app/motor_music.h"

#include "foc/foc_core.h"

#define MOTOR_MUSIC_PHASE_SCALE          1000000u
#define MOTOR_MUSIC_DEFAULT_SPEED_PERMILLE 2000u

/**
 * @brief 电机音乐运行上下文。
 */
typedef struct
{
    motor_music_state_t state;            /**< 当前播放状态，单位：无。 */
    const motor_music_note_t *notes;      /**< 当前乐谱指针。 */
    uint16_t note_count;                  /**< 当前乐谱音符数量，单位：个。 */
    uint16_t note_index;                  /**< 当前音符索引，单位：个。 */
    uint16_t repeat_left;                 /**< 剩余重复次数，单位：次。 */
    uint16_t speed_permille;              /**< 播放速度，单位：0.001 倍。 */
    uint16_t amplitude;                   /**< 播放幅值，单位：内部控制量。 */
    uint32_t note_elapsed_ticks;          /**< 当前音符已播放 tick 数，单位：tick。 */
    uint32_t note_duration_ticks;         /**< 当前音符目标 tick 数，单位：tick。 */
    uint32_t phase_accum;                 /**< 音频相位累加器，单位：微周期。 */
    uint32_t phase_step;                  /**< 每个调度 tick 的相位步进，单位：微周期。 */
    int32_t output_target;                /**< 当前 q 轴音乐目标，单位：内部控制量。 */
} motor_music_context_t;

static motor_music_context_t s_motor_music_ctx;

static const motor_music_note_t s_motor_music_nokia_tune[] =
{
    {.frequency_hz = 659u, .duration_ms = 220u},
    {.frequency_hz = 587u, .duration_ms = 240u},
    {.frequency_hz = 370u, .duration_ms = 480u},
    {.frequency_hz = 415u, .duration_ms = 480u},
    {.frequency_hz = 554u, .duration_ms = 240u},
    {.frequency_hz = 494u, .duration_ms = 240u},
    {.frequency_hz = 294u, .duration_ms = 480u},
    {.frequency_hz = 330u, .duration_ms = 480u},
    {.frequency_hz = 494u, .duration_ms = 240u},
    {.frequency_hz = 440u, .duration_ms = 240u},
    {.frequency_hz = 277u, .duration_ms = 480u},
    {.frequency_hz = 330u, .duration_ms = 480u},
    {.frequency_hz = 440u, .duration_ms = 960u},
};

/**
 * @brief 计算音符播放 tick 数。
 * @param duration_ms 音符原始时长，单位：毫秒。
 * @param tick_hz 调度频率，单位：Hz。
 * @param speed_permille 播放速度，单位：0.001 倍。
 * @return uint32_t 音符播放 tick 数，单位：tick。
 */
static uint32_t motor_music_calc_duration_ticks(uint16_t duration_ms,
                                                uint16_t tick_hz,
                                                uint16_t speed_permille)
{
    uint32_t scaled_duration_ms;
    uint32_t ticks;

    if (speed_permille == 0u)
    {
        speed_permille = 1000u;
    }

    scaled_duration_ms = (((uint32_t)duration_ms * 1000u) + ((uint32_t)speed_permille / 2u)) /
                         (uint32_t)speed_permille;
    if (scaled_duration_ms == 0u)
    {
        scaled_duration_ms = 1u;
    }

    ticks = ((scaled_duration_ms * (uint32_t)tick_hz) + 999u) / 1000u;
    if (ticks == 0u)
    {
        ticks = 1u;
    }

    return ticks;
}

/**
 * @brief 计算音符相位步进。
 * @param frequency_hz 音符频率，单位：Hz。
 * @param tick_hz 调度频率，单位：Hz。
 * @return uint32_t 相位步进，单位：微周期。
 */
static uint32_t motor_music_calc_phase_step(uint16_t frequency_hz, uint16_t tick_hz)
{
    if ((frequency_hz == 0u) || (tick_hz == 0u))
    {
        return 0u;
    }

    return (((uint32_t)frequency_hz * MOTOR_MUSIC_PHASE_SCALE) + ((uint32_t)tick_hz / 2u)) /
           (uint32_t)tick_hz;
}

/**
 * @brief 应用当前音符参数。
 * @param tick_hz 调度频率，单位：Hz。
 * @return void
 */
static void motor_music_apply_current_note(uint16_t tick_hz)
{
    const motor_music_note_t *note;

    if ((s_motor_music_ctx.notes == NULL) ||
        (s_motor_music_ctx.note_index >= s_motor_music_ctx.note_count))
    {
        MotorMusic_Stop();
        return;
    }

    note = &s_motor_music_ctx.notes[s_motor_music_ctx.note_index];
    s_motor_music_ctx.note_elapsed_ticks = 0u;
    s_motor_music_ctx.note_duration_ticks = motor_music_calc_duration_ticks(note->duration_ms,
                                                                            tick_hz,
                                                                            s_motor_music_ctx.speed_permille);
    s_motor_music_ctx.phase_accum = 0u;
    s_motor_music_ctx.phase_step = motor_music_calc_phase_step(note->frequency_hz, tick_hz);
    s_motor_music_ctx.output_target = 0;
}

/**
 * @brief 切换到下一个音符。
 * @param tick_hz 调度频率，单位：Hz。
 * @return void
 */
static void motor_music_advance_note(uint16_t tick_hz)
{
    s_motor_music_ctx.note_index++;
    if (s_motor_music_ctx.note_index >= s_motor_music_ctx.note_count)
    {
        if (s_motor_music_ctx.repeat_left > 1u)
        {
            s_motor_music_ctx.repeat_left--;
            s_motor_music_ctx.note_index = 0u;
        }
        else
        {
            MotorMusic_Stop();
            return;
        }
    }

    motor_music_apply_current_note(tick_hz);
}

/**
 * @brief 启动指定乐谱播放。
 * @param notes 乐谱指针。
 * @param note_count 音符数量，单位：个。
 * @param repeat 重复次数，单位：次。
 * @param speed_permille 播放速度，单位：0.001 倍。
 * @param amplitude_percent 输出幅值百分比，单位：%。
 * @return void
 */
static void motor_music_start_notes(const motor_music_note_t *notes,
                                    uint16_t note_count,
                                    uint16_t repeat,
                                    uint16_t speed_permille,
                                    uint8_t amplitude_percent)
{
    if ((notes == NULL) || (note_count == 0u) || (repeat == 0u))
    {
        return;
    }

    if (amplitude_percent > 20u)
    {
        amplitude_percent = 20u;
    }

    s_motor_music_ctx.state = MOTOR_MUSIC_STATE_PLAYING;
    s_motor_music_ctx.notes = notes;
    s_motor_music_ctx.note_count = note_count;
    s_motor_music_ctx.note_index = 0u;
    s_motor_music_ctx.repeat_left = repeat;
    s_motor_music_ctx.speed_permille = speed_permille;
    s_motor_music_ctx.amplitude = (uint16_t)(((uint32_t)OUT_MAX * amplitude_percent) / 100u);
    s_motor_music_ctx.note_elapsed_ticks = 0u;
    s_motor_music_ctx.note_duration_ticks = 1u;
    s_motor_music_ctx.phase_accum = 0u;
    s_motor_music_ctx.phase_step = 0u;
    s_motor_music_ctx.output_target = 0;
    motor_music_apply_current_note(MOTOR_MUSIC_DEFAULT_TICK_HZ);
}

/**
 * @brief 初始化电机音乐状态机。
 * @return void
 */
void MotorMusic_Init(void)
{
    MotorMusic_Stop();
}

/**
 * @brief 启动上电音乐。
 * @return void
 */
void MotorMusic_StartPowerOn(void)
{
#if MOTOR_MUSIC_POWER_ON_ENABLE
    motor_music_start_notes(s_motor_music_nokia_tune,
                            (uint16_t)(sizeof(s_motor_music_nokia_tune) / sizeof(s_motor_music_nokia_tune[0])),
                            1u,
                            MOTOR_MUSIC_DEFAULT_SPEED_PERMILLE,
                            MOTOR_MUSIC_DEFAULT_AMPLITUDE_PERCENT);
#endif
}

/**
 * @brief 停止当前音乐播放。
 * @return void
 */
void MotorMusic_Stop(void)
{
    s_motor_music_ctx.state = MOTOR_MUSIC_STATE_IDLE;
    s_motor_music_ctx.notes = NULL;
    s_motor_music_ctx.note_count = 0u;
    s_motor_music_ctx.note_index = 0u;
    s_motor_music_ctx.repeat_left = 0u;
    s_motor_music_ctx.speed_permille = 1000u;
    s_motor_music_ctx.amplitude = 0u;
    s_motor_music_ctx.note_elapsed_ticks = 0u;
    s_motor_music_ctx.note_duration_ticks = 0u;
    s_motor_music_ctx.phase_accum = 0u;
    s_motor_music_ctx.phase_step = 0u;
    s_motor_music_ctx.output_target = 0;
}

/**
 * @brief 推进电机音乐状态机。
 * @param tick_hz 调用频率，单位：Hz。
 * @return int32_t 当前 q 轴音乐目标值，单位：内部控制量。
 */
int32_t MotorMusic_Poll(uint16_t tick_hz)
{
    if (s_motor_music_ctx.state != MOTOR_MUSIC_STATE_PLAYING)
    {
        return 0;
    }

    if (tick_hz == 0u)
    {
        tick_hz = MOTOR_MUSIC_DEFAULT_TICK_HZ;
    }

    if (s_motor_music_ctx.note_elapsed_ticks >= s_motor_music_ctx.note_duration_ticks)
    {
        motor_music_advance_note(tick_hz);
        if (s_motor_music_ctx.state != MOTOR_MUSIC_STATE_PLAYING)
        {
            return 0;
        }
    }

    if (s_motor_music_ctx.phase_step == 0u)
    {
        s_motor_music_ctx.output_target = 0;
    }
    else
    {
        s_motor_music_ctx.phase_accum += s_motor_music_ctx.phase_step;
        if (s_motor_music_ctx.phase_accum >= MOTOR_MUSIC_PHASE_SCALE)
        {
            s_motor_music_ctx.phase_accum -= MOTOR_MUSIC_PHASE_SCALE;
        }

        if (s_motor_music_ctx.phase_accum < (MOTOR_MUSIC_PHASE_SCALE / 2u))
        {
            s_motor_music_ctx.output_target = (int32_t)s_motor_music_ctx.amplitude;
        }
        else
        {
            s_motor_music_ctx.output_target = -((int32_t)s_motor_music_ctx.amplitude);
        }
    }

    s_motor_music_ctx.note_elapsed_ticks++;
    return s_motor_music_ctx.output_target;
}

/**
 * @brief 判断音乐是否正在播放。
 * @return uint8_t 正在播放返回 1，否则返回 0。
 */
uint8_t MotorMusic_IsPlaying(void)
{
    if (s_motor_music_ctx.state == MOTOR_MUSIC_STATE_PLAYING)
    {
        return 1u;
    }

    return 0u;
}
