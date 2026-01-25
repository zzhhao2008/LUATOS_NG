#include "luat_base.h"
#include "luat_pwm.h"
#include <math.h>
#include "driver/ledc.h"
#include "esp_timer.h"
#include "luat_log.h"
#define LUAT_LOG_TAG "pwm"

// ESP32-S3 只有 8 个低速 LEDC 通道
#define MAX_PWM_CHANNELS 8

typedef struct pwm_cont
{
    luat_pwm_conf_t pc;
    uint8_t is_opened;
    uint8_t channel_num; // LEDC 通道号 (0-7)
    uint8_t timer_num;   // LEDC timer 号 (0-3)
    int duty_resolution; // 存储当前的 duty 分辨率
    bool is_fading;      // 标记是否正在进行 fade
} pwm_conf_t;

static pwm_conf_t luat_pwm_idf[MAX_PWM_CHANNELS];
static bool fade_func_installed = false;

#define LEDC_LL_FRACTIONAL_BITS (8)
#define LEDC_LL_FRACTIONAL_MAX ((1 << LEDC_LL_FRACTIONAL_BITS) - 1)
#define LEDC_TIMER_DIV_NUM_MAX (0x3FFFF)
#define LEDC_IS_DIV_INVALID(div) ((div) <= LEDC_LL_FRACTIONAL_MAX || (div) > LEDC_TIMER_DIV_NUM_MAX)

static inline uint32_t ilog2(uint32_t i)
{
    assert(i > 0);
    uint32_t log = 0;
    while (i >>= 1)
    {
        ++log;
    }
    return log;
}

static inline uint32_t ledc_calculate_divisor(uint32_t src_clk_freq, int freq_hz, uint32_t precision)
{
    uint64_t fp = freq_hz;
    fp *= precision;
    return (((uint64_t)src_clk_freq << LEDC_LL_FRACTIONAL_BITS) + (fp / 2)) / fp;
}

static uint32_t ledc_find_suitable_duty_resolution2(uint32_t src_clk_freq, uint32_t timer_freq)
{
    uint32_t div = (src_clk_freq + timer_freq / 2) / timer_freq; // rounded
    uint32_t duty_resolution = ilog2(div);
    if (duty_resolution >= SOC_LEDC_TIMER_BIT_WIDTH)
    {
        duty_resolution = SOC_LEDC_TIMER_BIT_WIDTH - 1;
    }
    uint32_t div_param = ledc_calculate_divisor(src_clk_freq, timer_freq, 1 << duty_resolution);
    if (LEDC_IS_DIV_INVALID(div_param))
    {
        div = src_clk_freq / timer_freq; // truncated
        duty_resolution = ilog2(div);
        if (duty_resolution > SOC_LEDC_TIMER_BIT_WIDTH - 1)
        {
            duty_resolution = SOC_LEDC_TIMER_BIT_WIDTH - 1;
        }
        div_param = ledc_calculate_divisor(src_clk_freq, timer_freq, 1 << duty_resolution);
        if (LEDC_IS_DIV_INVALID(div_param))
        {
            duty_resolution = 0;
        }
    }
    return duty_resolution;
}

// 停止当前的 fade 过程
static void stop_current_fade(int index)
{
    if (index >= 0 && index < MAX_PWM_CHANNELS && luat_pwm_idf[index].is_fading)
    {
        ledc_fade_stop(LEDC_LOW_SPEED_MODE, luat_pwm_idf[index].channel_num);
        luat_pwm_idf[index].is_fading = false;
    }
}

// 为指定 GPIO 分配合适的 LEDC 通道
static int allocate_pwm_channel(int gpio)
{
    // ESP32-S3 有 8 个低速通道，需要合理分配
    for (int i = 0; i < MAX_PWM_CHANNELS; i++)
    {
        if (!luat_pwm_idf[i].is_opened)
        {
            // 通道号直接使用索引 0-7
            luat_pwm_idf[i].channel_num = i;
            // timer 分配：4个 timer，每个 timer 支持 2 个通道
            luat_pwm_idf[i].timer_num = i / 2;
            luat_pwm_idf[i].pc.channel = gpio;
            return i;
        }
    }
    return -1;
}

// 释放 PWM 通道
static void free_pwm_channel(int index)
{
    if (index >= 0 && index < MAX_PWM_CHANNELS)
    {
        luat_pwm_idf[index].is_opened = 0;
        memset(&luat_pwm_idf[index].pc, 0, sizeof(luat_pwm_conf_t));
    }
}

// 确保 fade 功能已安装
static void ensure_fade_func_installed(void)
{
    if (!fade_func_installed)
    {
        esp_err_t ret = ledc_fade_func_install(0);
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE)
        {
            fade_func_installed = true;
        }
        else
        {
            LLOGE("ledc_fade_func_install failed: %d", ret);
        }
    }
}

int luat_pwm_setup(luat_pwm_conf_t *conf)
{
    int duty_resolution = 0;
    int channel_index = -1;
    int ret = -1;

    if (conf->channel < 0)
    {
        LLOGE("Invalid GPIO channel: %d", conf->channel);
        return -1;
    }

    // 检查是否已经为该 GPIO 分配了通道
    for (int i = 0; i < MAX_PWM_CHANNELS; i++)
    {
        if (luat_pwm_idf[i].is_opened && luat_pwm_idf[i].pc.channel == conf->channel)
        {
            channel_index = i;
            break;
        }
    }

    // 如果没有分配，则分配新的通道
    if (channel_index < 0)
    {
        channel_index = allocate_pwm_channel(conf->channel);
        if (channel_index < 0)
        {
            LLOGE("No available PWM channels! Maximum %d channels supported", MAX_PWM_CHANNELS);
            return -1;
        }
    }

    // 限制占空比范围
    if (conf->pulse > conf->precision)
    {
        conf->pulse = conf->precision;
    }

    // 停止当前可能的 fade 过程
    stop_current_fade(channel_index);

    // 计算合适的 duty 分辨率
    duty_resolution = ledc_find_suitable_duty_resolution2(80 * 1000 * 1000, conf->period);

    // 检查是否需要重新配置 timer
    bool need_reconfig_timer = true;
    if (luat_pwm_idf[channel_index].is_opened)
    {
        // 检查频率和分辨率是否相同
        if (conf->period == luat_pwm_idf[channel_index].pc.period &&
            conf->precision == luat_pwm_idf[channel_index].pc.precision)
        {
            need_reconfig_timer = false;
        }
    }

    if (need_reconfig_timer)
    {
        // 配置 timer - 只使用低速模式
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE, // ESP32-S3 只支持低速模式
            .timer_num = luat_pwm_idf[channel_index].timer_num,
            .freq_hz = conf->period,
            .clk_cfg = LEDC_AUTO_CLK,
            .duty_resolution = duty_resolution};

        ret = ledc_timer_config(&ledc_timer);
        if (ret)
        {
            LLOGE("ledc_timer_config failed: %d", ret);
            free_pwm_channel(channel_index);
            return -1;
        }

        // 配置 channel - 只使用低速模式
        ledc_channel_config_t ledc_channel = {
            .speed_mode = LEDC_LOW_SPEED_MODE, // ESP32-S3 只支持低速模式
            .channel = luat_pwm_idf[channel_index].channel_num,
            .timer_sel = luat_pwm_idf[channel_index].timer_num,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = conf->channel,
            .duty = 0, // 初始占空比设为 0
            .hpoint = 0,
        };

        ret = ledc_channel_config(&ledc_channel);
        if (ret)
        {
            LLOGE("ledc_channel_config failed: %d", ret);
            free_pwm_channel(channel_index);
            return -1;
        }
    }

    // 计算并设置占空比
    int duty = (conf->pulse * (1 << duty_resolution)) / conf->precision;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, luat_pwm_idf[channel_index].channel_num, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, luat_pwm_idf[channel_index].channel_num);

    // 保存配置
    memcpy(&luat_pwm_idf[channel_index].pc, conf, sizeof(luat_pwm_conf_t));
    luat_pwm_idf[channel_index].is_opened = 1;
    luat_pwm_idf[channel_index].duty_resolution = duty_resolution;
    luat_pwm_idf[channel_index].is_fading = false;

    // 如果配置了 fade，启动 fade 过程
    if (conf->fade_time > 0 && conf->target_pulse >= 0)
    {
        return luat_pwm_fade(conf->channel, conf->target_pulse, conf->fade_time);
    }

    return 0;
}

int luat_pwm_fade(int channel, int target_duty, int time_ms)
{
    if (channel < 0 || time_ms <= 0 || target_duty < 0)
    {
        LLOGE("Invalid fade parameters: channel=%d, target_duty=%d, time_ms=%d",
              channel, target_duty, time_ms);
        return -1;
    }

    // 查找通道
    int channel_index = -1;
    for (int i = 0; i < MAX_PWM_CHANNELS; i++)
    {
        if (luat_pwm_idf[i].is_opened && luat_pwm_idf[i].pc.channel == channel)
        {
            channel_index = i;
            break;
        }
    }

    if (channel_index < 0)
    {
        LLOGE("PWM channel %d not found or not opened", channel);
        return -1;
    }

    // 限制目标占空比
    if (target_duty > luat_pwm_idf[channel_index].pc.precision)
    {
        target_duty = luat_pwm_idf[channel_index].pc.precision;
    }

    // 停止当前的 fade 过程
    stop_current_fade(channel_index);

    // 确保 fade 功能已安装
    ensure_fade_func_installed();
    if (!fade_func_installed)
    {
        LLOGE("Fade function not installed");
        return -1;
    }

    // 计算目标 duty 值
    int target_duty_value = (target_duty * (1 << luat_pwm_idf[channel_index].duty_resolution)) /
                            luat_pwm_idf[channel_index].pc.precision;

    // 修正：使用 ledc_set_fade_with_time 代替 ledc_set_fade_time
    esp_err_t ret = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE,
                                            luat_pwm_idf[channel_index].channel_num,
                                            target_duty_value, time_ms);
    if (ret != ESP_OK)
    {
        LLOGE("ledc_set_fade_with_time failed: %d", ret);
        return -1;
    }

    // 启动 fade
    ret = ledc_fade_start(LEDC_LOW_SPEED_MODE,
                          luat_pwm_idf[channel_index].channel_num,
                          LEDC_FADE_NO_WAIT);
    if (ret != ESP_OK)
    {
        LLOGE("ledc_fade_start failed: %d", ret);
        return -1;
    }

    // 更新状态
    luat_pwm_idf[channel_index].is_fading = true;
    luat_pwm_idf[channel_index].pc.target_pulse = target_duty;
    luat_pwm_idf[channel_index].pc.fade_time = time_ms;

    return 0;
}
int luat_pwm_stop_fade(int channel)
{
    if (channel < 0)
    {
        return -1;
    }

    // 查找通道
    for (int i = 0; i < MAX_PWM_CHANNELS; i++)
    {
        if (luat_pwm_idf[i].is_opened && luat_pwm_idf[i].pc.channel == channel)
        {
            stop_current_fade(i);
            return 0;
        }
    }

    return -1;
}

int luat_pwm_close(int channel)
{
    if (channel < 0)
    {
        return -1;
    }

    // 查找通道
    int channel_index = -1;
    for (int i = 0; i < MAX_PWM_CHANNELS; i++)
    {
        if (luat_pwm_idf[i].is_opened && luat_pwm_idf[i].pc.channel == channel)
        {
            channel_index = i;
            break;
        }
    }

    if (channel_index < 0)
    {
        return -1;
    }

    // 停止 fade
    stop_current_fade(channel_index);

    // 停止 PWM
    int ret = ledc_stop(LEDC_LOW_SPEED_MODE,
                        luat_pwm_idf[channel_index].channel_num, 0);
    if (ret)
    {
        LLOGW("ledc_stop failed: %d", ret);
    }

    // 重置 GPIO
    gpio_reset_pin(channel);

    // 释放通道
    free_pwm_channel(channel_index);

    return 0;
}

int luat_pwm_capture(int channel, int freq)
{
    return -1; // 暂不实现
}