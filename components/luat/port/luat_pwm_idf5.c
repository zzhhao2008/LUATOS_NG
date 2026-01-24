#include "luat_base.h"
#include "luat_pwm.h"
#include <math.h>
#include "driver/ledc.h"
#include "esp_timer.h"
#include "luat_log.h"
#define LUAT_LOG_TAG "pwm"

// 定义定时器和通道数量
#ifdef CONFIG_IDF_TARGET_ESP32S3
    #define PWM_TIMER_COUNT LEDC_TIMER_MAX
    #define PWM_CHANNEL_COUNT LEDC_CHANNEL_MAX
#elif CONFIG_IDF_TARGET_ESP32C3
    #define PWM_TIMER_COUNT LEDC_TIMER_MAX
    #define PWM_CHANNEL_COUNT LEDC_CHANNEL_MAX
#else
    #define PWM_TIMER_COUNT LEDC_TIMER_MAX
    #define PWM_CHANNEL_COUNT LEDC_CHANNEL_MAX
#endif

// 定义一个映射表，将GPIO引脚号映射到LEDC通道
typedef struct {
    int gpio_num;                 // GPIO引脚号
    ledc_channel_t ledc_channel;  // 对应的LEDC通道
    uint8_t is_used;              // 是否被占用
} gpio_to_ledc_map_t;

static gpio_to_ledc_map_t gpio_ledc_map[PWM_CHANNEL_COUNT];

// 定时器状态结构
typedef struct {
    luat_pwm_conf_t timer_conf;  // 保存定时器的配置（频率、精度等）
    uint8_t is_used;             // 是否被占用
    int duty_resolution;         // 当前的duty分辨率
    bool is_fading;              // 标记是否正在进行fade
} pwm_timer_t;

// 通道状态结构
typedef struct {
    luat_pwm_conf_t pc;          // 通道配置（占空比等）
    uint8_t is_opened;           // 是否开启
    ledc_timer_t bound_timer;    // 绑定的定时器编号
    int gpio_num;                // 对应的GPIO引脚号
} pwm_channel_t;

static pwm_timer_t luat_pwm_timers[PWM_TIMER_COUNT];      // 定时器状态数组
static pwm_channel_t luat_pwm_channels[PWM_CHANNEL_COUNT]; // 通道状态数组

#define LEDC_LL_FRACTIONAL_BITS (8)
#define LEDC_LL_FRACTIONAL_MAX     ((1 << LEDC_LL_FRACTIONAL_BITS) - 1)
#define LEDC_TIMER_DIV_NUM_MAX    (0x3FFFF)
#define LEDC_IS_DIV_INVALID(div)  ((div) <= LEDC_LL_FRACTIONAL_MAX || (div) > LEDC_TIMER_DIV_NUM_MAX)

static inline uint32_t ilog2(uint32_t i)
{
    assert(i > 0);
    uint32_t log = 0;
    while (i >>= 1) {
        ++log;
    }
    return log;
}

static inline uint32_t ledc_calculate_divisor(uint32_t src_clk_freq, int freq_hz, uint32_t precision)
{
    uint64_t fp = freq_hz;
    fp *= precision;
    return ( ( (uint64_t) src_clk_freq << LEDC_LL_FRACTIONAL_BITS ) + (fp / 2 ) ) / fp;
}

// https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf#ledpwm
static uint32_t ledc_find_suitable_duty_resolution2(uint32_t src_clk_freq, uint32_t timer_freq)
{
    uint32_t div = (src_clk_freq + timer_freq / 2) / timer_freq; // rounded
    uint32_t duty_resolution = ilog2(div);
    if ( duty_resolution >= SOC_LEDC_TIMER_BIT_WIDTH) {
        duty_resolution = SOC_LEDC_TIMER_BIT_WIDTH - 1;
    }
    uint32_t div_param = ledc_calculate_divisor(src_clk_freq, timer_freq, 1 << duty_resolution);
    if (LEDC_IS_DIV_INVALID(div_param)) {
        div = src_clk_freq / timer_freq; // truncated
        duty_resolution = ilog2(div);
        if ( duty_resolution > SOC_LEDC_TIMER_BIT_WIDTH - 1) {
            duty_resolution = SOC_LEDC_TIMER_BIT_WIDTH - 1;
        }
        div_param = ledc_calculate_divisor(src_clk_freq, timer_freq, 1 << duty_resolution);
        if (LEDC_IS_DIV_INVALID(div_param)) {
            duty_resolution = 0;
        }
    }
    return duty_resolution;
}

// 根据GPIO引脚号查找或分配LEDC通道
static ledc_channel_t find_or_allocate_ledc_channel(int gpio_num) {
    // 先查找是否已经有这个GPIO号的映射
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        if (gpio_ledc_map[i].gpio_num == gpio_num && gpio_ledc_map[i].is_used) {
            return gpio_ledc_map[i].ledc_channel;
        }
    }
    
    // 没有找到，尝试分配一个新的
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        if (!gpio_ledc_map[i].is_used) {
            gpio_ledc_map[i].gpio_num = gpio_num;
            gpio_ledc_map[i].ledc_channel = i;  // 顺序分配LEDC通道
            gpio_ledc_map[i].is_used = 1;
            return gpio_ledc_map[i].ledc_channel;
        }
    }
    
    return -1;  // 没有可用的LEDC通道
}

// 释放GPIO到LEDC通道的映射
static void free_gpio_ledc_mapping(int gpio_num) {
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        if (gpio_ledc_map[i].gpio_num == gpio_num && gpio_ledc_map[i].is_used) {
            gpio_ledc_map[i].is_used = 0;
            gpio_ledc_map[i].gpio_num = -1;
        }
    }
}

// 停止当前的fade过程
static void stop_current_fade(ledc_timer_t timer_index) {
    if (timer_index >= 0 && timer_index < PWM_TIMER_COUNT && luat_pwm_timers[timer_index].is_fading) {
        ledc_fade_func_uninstall();
        luat_pwm_timers[timer_index].is_fading = false;
    }
}

// 查找是否已有相同配置的定时器
static ledc_timer_t find_existing_timer(luat_pwm_conf_t *conf) {
    for (int i = 0; i < PWM_TIMER_COUNT; i++) {
        if (luat_pwm_timers[i].is_used &&
            luat_pwm_timers[i].timer_conf.period == conf->period &&
            luat_pwm_timers[i].timer_conf.precision == conf->precision &&
            luat_pwm_timers[i].timer_conf.pnum == conf->pnum) {
            return i;
        }
    }
    return -1;
}

// 查找空闲的定时器
static ledc_timer_t find_free_timer() {
    for (int i = 0; i < PWM_TIMER_COUNT; i++) {
        if (!luat_pwm_timers[i].is_used) {
            return i;
        }
    }
    return -1;
}

int luat_pwm_setup(luat_pwm_conf_t *conf){
    int duty_resolution = 0;
    ledc_timer_t timer_idx = -1;
    ledc_channel_t channel_idx = -1;
    int ret = -1;
    
    if (conf->channel < 0)
        return -1;
    
    // 分配或获取对应的LEDC通道号
    channel_idx = find_or_allocate_ledc_channel(conf->channel);
    if (channel_idx < 0) {
        LLOGE("Failed to allocate LEDC channel for GPIO %d", conf->channel);
        return -1;
    }
    
    // 检查该通道是否已经被打开
    if (luat_pwm_channels[channel_idx].is_opened) {
        // 如果配置不同，则需要重新配置
        if (luat_pwm_channels[channel_idx].pc.period != conf->period ||
            luat_pwm_channels[channel_idx].pc.precision != conf->precision ||
            luat_pwm_channels[channel_idx].pc.pnum != conf->pnum) {
            // 先关闭现有配置
            luat_pwm_close(conf->channel);
        } else {
            // 配置相同，只需更新占空比
            timer_idx = luat_pwm_channels[channel_idx].bound_timer;
            duty_resolution = luat_pwm_timers[timer_idx].duty_resolution;
            int duty = (conf->pulse * (1 << duty_resolution)) / conf->precision;
            
            ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_idx, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_idx);
            
            memcpy(&luat_pwm_channels[channel_idx].pc, conf, sizeof(luat_pwm_conf_t));
            
            // 如果配置了fade，启动fade过程
            if (conf->fade_time > 0 && conf->target_pulse >= 0) {
                return luat_pwm_fade(conf->channel, conf->target_pulse, conf->fade_time);
            }
            
            return 0;
        }
    }
    
    // 检查是否需要限制脉冲值
    if (conf->pulse > conf->precision) {
        conf->pulse = conf->precision;
    }
    
    // 尝试找到一个具有相同配置的定时器
    timer_idx = find_existing_timer(conf);
    
    // 如果没有找到现有定时器，则尝试分配一个新的
    if (timer_idx < 0) {
        timer_idx = find_free_timer();
        if (timer_idx < 0) {
            LLOGE("Too many PWM configurations! Only %d timers supported", PWM_TIMER_COUNT);
            return -1;
        }
        
        // 配置新的定时器
        duty_resolution = ledc_find_suitable_duty_resolution2(80*1000*1000, conf->period);
        
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_num = timer_idx,
            .freq_hz = conf->period,
            .clk_cfg = LEDC_AUTO_CLK,
            .duty_resolution = duty_resolution
        };
        
        ret = ledc_timer_config(&ledc_timer);
        if (ret) {
            LLOGE("ledc_timer_config failed: %d", ret);
            return -1;
        }
        
        // 更新定时器状态
        memcpy(&luat_pwm_timers[timer_idx].timer_conf, conf, sizeof(luat_pwm_conf_t));
        luat_pwm_timers[timer_idx].is_used = 1;
        luat_pwm_timers[timer_idx].duty_resolution = duty_resolution;
        luat_pwm_timers[timer_idx].is_fading = false;
    } else {
        // 使用现有的定时器配置
        duty_resolution = luat_pwm_timers[timer_idx].duty_resolution;
    }
    
    int duty = (conf->pulse * (1 << duty_resolution)) / conf->precision;
    
    // 配置通道
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel_idx,
        .timer_sel = timer_idx,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = conf->channel,  // 使用GPIO引脚号
        .duty = duty,
        .hpoint = 0,
    };
    
    ret = ledc_channel_config(&ledc_channel);
    if (ret) {
        LLOGE("ledc_channel_config failed: %d", ret);
        // 释放映射
        free_gpio_ledc_mapping(conf->channel);
        return -1;
    }
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_idx, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_idx);
    
    // 更新通道状态
    memcpy(&luat_pwm_channels[channel_idx].pc, conf, sizeof(luat_pwm_conf_t));
    luat_pwm_channels[channel_idx].is_opened = 1;
    luat_pwm_channels[channel_idx].bound_timer = timer_idx;
    luat_pwm_channels[channel_idx].gpio_num = conf->channel;
    
    // 如果配置了fade，启动fade过程
    if (conf->fade_time > 0 && conf->target_pulse >= 0) {
        return luat_pwm_fade(conf->channel, conf->target_pulse, conf->fade_time);
    }
    
    return 0;
}

int luat_pwm_fade(int channel, int target_duty, int time_ms) {
    if (channel < 0 || target_duty < 0) {
        LLOGE("Invalid fade parameters: channel=%d, target_duty=%d, time_ms=%d", 
              channel, target_duty, time_ms);
        return -1;
    }
    
    // 根据GPIO引脚号查找对应的LEDC通道
    ledc_channel_t channel_idx = -1;
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        if (gpio_ledc_map[i].gpio_num == channel && gpio_ledc_map[i].is_used) {
            channel_idx = gpio_ledc_map[i].ledc_channel;
            break;
        }
    }
    
    if (channel_idx < 0) {
        LLOGE("GPIO %d not allocated for LEDC", channel);
        return -1;
    }

    if (!luat_pwm_channels[channel_idx].is_opened) {
        LLOGE("PWM channel %d not found or not opened", channel);
        return -1;
    }
    
    ledc_timer_t timer_idx = luat_pwm_channels[channel_idx].bound_timer;
    
    // 限制目标占空比
    if (target_duty > luat_pwm_channels[channel_idx].pc.precision) {
        target_duty = luat_pwm_channels[channel_idx].pc.precision;
    }
    
    // 停止当前的fade过程
    stop_current_fade(timer_idx);
    
    // 安装fade功能
    esp_err_t ret = ledc_fade_func_install(LEDC_LOW_SPEED_MODE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) { // ESP_ERR_INVALID_STATE表示已经安装
        LLOGE("ledc_fade_func_install failed: %d", ret);
        return -1;
    }
    
    // 计算目标duty值
    int target_duty_value = (target_duty * (1 << luat_pwm_timers[timer_idx].duty_resolution)) / luat_pwm_channels[channel_idx].pc.precision;
    
    // 设置fade
    ret = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, channel_idx, target_duty_value, time_ms);
    if (ret != ESP_OK) {
        LLOGE("ledc_set_fade_with_time failed: %d", ret);
        return -1;
    }
    
    // 启动fade
    ret = ledc_fade_start(LEDC_LOW_SPEED_MODE, channel_idx, LEDC_FADE_NO_WAIT);
    if (ret != ESP_OK) {
        LLOGE("ledc_fade_start failed: %d", ret);
        return -1;
    }
    
    // 更新状态
    luat_pwm_timers[timer_idx].is_fading = true;
    luat_pwm_channels[channel_idx].pc.target_pulse = target_duty;
    
    return 0;
}

int luat_pwm_stop_fade(int channel) {
    if (channel < 0) {
        return -1;
    }
    
    // 根据GPIO引脚号查找对应的LEDC通道
    ledc_channel_t channel_idx = -1;
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        if (gpio_ledc_map[i].gpio_num == channel && gpio_ledc_map[i].is_used) {
            channel_idx = gpio_ledc_map[i].ledc_channel;
            break;
        }
    }
    
    if (channel_idx < 0) {
        return -1;
    }

    if (!luat_pwm_channels[channel_idx].is_opened) {
        return -1;
    }
    
    ledc_timer_t timer_idx = luat_pwm_channels[channel_idx].bound_timer;
    
    stop_current_fade(timer_idx);
    
    return 0;
}

int luat_pwm_close(int channel){
    if (channel < 0) {
        return -1;
    }
    
    // 根据GPIO引脚号查找对应的LEDC通道
    ledc_channel_t channel_idx = -1;
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        if (gpio_ledc_map[i].gpio_num == channel && gpio_ledc_map[i].is_used) {
            channel_idx = gpio_ledc_map[i].ledc_channel;
            break;
        }
    }
    
    if (channel_idx < 0) {
        return -1;
    }

    if (!luat_pwm_channels[channel_idx].is_opened) {
        return -1;
    }
    
    ledc_timer_t timer_idx = luat_pwm_channels[channel_idx].bound_timer;
    
    // 停止fade
    stop_current_fade(timer_idx);
    
    int ret = ledc_stop(LEDC_LOW_SPEED_MODE, channel_idx, 0);
    if (ret) {
        return -1;
    }
    
    gpio_reset_pin(luat_pwm_channels[channel_idx].gpio_num);
    
    // 清除通道状态
    luat_pwm_channels[channel_idx].is_opened = 0;
    memset(&luat_pwm_channels[channel_idx].pc, 0, sizeof(luat_pwm_conf_t));
    luat_pwm_channels[channel_idx].bound_timer = -1;
    luat_pwm_channels[channel_idx].gpio_num = -1;
    
    // 清理GPIO到LEDC通道的映射
    free_gpio_ledc_mapping(channel);
    
    // 检查是否有其他通道正在使用此定时器
    int other_channels_using_timer = 0;
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        if (luat_pwm_channels[i].is_opened && 
            luat_pwm_channels[i].bound_timer == timer_idx) {
            other_channels_using_timer++;
        }
    }
    
    // 如果没有其他通道使用此定时器，则释放定时器
    if (other_channels_using_timer == 0) {
        luat_pwm_timers[timer_idx].is_used = 0;
        memset(&luat_pwm_timers[timer_idx].timer_conf, 0, sizeof(luat_pwm_conf_t));
        luat_pwm_timers[timer_idx].duty_resolution = 0;
        luat_pwm_timers[timer_idx].is_fading = false;
    }
    
    return 0;
}

int luat_pwm_capture(int channel, int freq){
    return -1;
}