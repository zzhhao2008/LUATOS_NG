#include "luat_base.h"
#include "luat_pwm.h"
#include <math.h>
#include "driver/ledc.h"
#include "esp_timer.h"
#include "luat_log.h"
#define LUAT_LOG_TAG "pwm"

// 定义定时器和通道数量
#ifdef CONFIG_IDF_TARGET_ESP32S3
    #define PWM_TIMER_COUNT LEDC_TIMER_3  // ESP32-S3最大支持4个定时器(0-3)
    #define PWM_CHANNEL_COUNT LEDC_CHANNEL_7  // ESP32-S3最大支持8个通道(0-7)
#elif CONFIG_IDF_TARGET_ESP32C3
    #define PWM_TIMER_COUNT LEDC_TIMER_1  // ESP32-C3最大支持2个定时器(0-1)
    #define PWM_CHANNEL_COUNT LEDC_CHANNEL_3  // ESP32-C3最大支持4个通道(0-3)
#else  // ESP32
    #define PWM_TIMER_COUNT LEDC_TIMER_3  // ESP32最大支持4个定时器(0-3)
    #define PWM_CHANNEL_COUNT LEDC_CHANNEL_7  // ESP32最大支持8个通道(0-7)
#endif

// 定义一个映射表，将GPIO引脚号映射到LEDC通道
typedef struct {
    int gpio_num;                 // GPIO引脚号
    ledc_channel_t ledc_channel;  // 对应的LEDC通道
    uint8_t is_used;              // 是否被占用
} gpio_to_ledc_map_t;

static gpio_to_ledc_map_t gpio_ledc_map[PWM_CHANNEL_COUNT + 1];  // +1确保数组大小正确

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
    ledc_channel_t ledc_channel; // 对应的LEDC硬件通道
} pwm_channel_t;

static pwm_timer_t luat_pwm_timers[PWM_TIMER_COUNT + 1];      // 定时器状态数组
static pwm_channel_t luat_pwm_channels[PWM_CHANNEL_COUNT + 1]; // 通道状态数组

// 初始化映射表
static void init_gpio_ledc_map(void) {
    static bool initialized = false;
    if (initialized) return;
    
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        gpio_ledc_map[i].gpio_num = -1;
        gpio_ledc_map[i].ledc_channel = i;  // 使用硬件通道号
        gpio_ledc_map[i].is_used = 0;
        
        luat_pwm_channels[i].is_opened = 0;
        luat_pwm_channels[i].bound_timer = -1;
        luat_pwm_channels[i].gpio_num = -1;
        luat_pwm_channels[i].ledc_channel = i;
    }
    
    for (int i = 0; i <= PWM_TIMER_COUNT; i++) {
        luat_pwm_timers[i].is_used = 0;
        luat_pwm_timers[i].is_fading = false;
        luat_pwm_timers[i].duty_resolution = 0;
    }
    
    initialized = true;
}

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
static ledc_channel_t find_or_allocate_ledc_channel(int gpio_num, ledc_timer_t timer_idx) {
    // 先查找是否已经有这个GPIO号的映射
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (gpio_ledc_map[i].gpio_num == gpio_num && gpio_ledc_map[i].is_used) {
            return gpio_ledc_map[i].ledc_channel;
        }
    }
    
    // 没有找到，尝试分配一个新的（需要考虑ESP32的通道绑定限制）
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (!gpio_ledc_map[i].is_used) {
            gpio_ledc_map[i].gpio_num = gpio_num;
            gpio_ledc_map[i].ledc_channel = i;  // 使用硬件通道号
            gpio_ledc_map[i].is_used = 1;
            return gpio_ledc_map[i].ledc_channel;
        }
    }
    
    return LEDC_CHANNEL_MAX;  // 无效通道
}

// 释放GPIO到LEDC通道的映射
static void free_gpio_ledc_mapping(int gpio_num) {
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (gpio_ledc_map[i].gpio_num == gpio_num && gpio_ledc_map[i].is_used) {
            gpio_ledc_map[i].is_used = 0;
            gpio_ledc_map[i].gpio_num = -1;
        }
    }
}

// 停止当前的fade过程
static void stop_current_fade(ledc_timer_t timer_index) {
    if (timer_index <= PWM_TIMER_COUNT && luat_pwm_timers[timer_index].is_fading) {
        ledc_fade_func_uninstall();
        luat_pwm_timers[timer_index].is_fading = false;
    }
}

// 查找是否已有相同配置的定时器
static ledc_timer_t find_existing_timer(luat_pwm_conf_t *conf) {
    for (int i = 0; i <= PWM_TIMER_COUNT; i++) {
        if (luat_pwm_timers[i].is_used &&
            luat_pwm_timers[i].timer_conf.period == conf->period &&
            luat_pwm_timers[i].timer_conf.precision == conf->precision) {
            return i;
        }
    }
    return LEDC_TIMER_MAX;  // 无效定时器
}

// 查找空闲的定时器
static ledc_timer_t find_free_timer() {
    for (int i = 0; i <= PWM_TIMER_COUNT; i++) {
        if (!luat_pwm_timers[i].is_used) {
            return i;
        }
    }
    return LEDC_TIMER_MAX;  // 无效定时器
}

int luat_pwm_setup(luat_pwm_conf_t *conf){
    init_gpio_ledc_map();
    
    int duty_resolution = 0;
    ledc_timer_t timer_idx = LEDC_TIMER_MAX;
    ledc_channel_t channel_idx = LEDC_CHANNEL_MAX;
    int ret = -1;
    
    if (!conf) {
        LLOGE("NULL config pointer");
        return -1;
    }
    
    int gpio_num = conf->channel;  // conf->channel存储的是GPIO号，不是通道号
    
    // 检查该GPIO是否已经被打开
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (luat_pwm_channels[i].gpio_num == gpio_num && luat_pwm_channels[i].is_opened) {
            // 如果配置不同，则需要重新配置
            if (luat_pwm_channels[i].pc.period != conf->period ||
                luat_pwm_channels[i].pc.precision != conf->precision) {
                // 先关闭现有配置
                luat_pwm_close(gpio_num);
            } else {
                // 配置相同，只需更新占空比
                timer_idx = luat_pwm_channels[i].bound_timer;
                channel_idx = luat_pwm_channels[i].ledc_channel;
                duty_resolution = luat_pwm_timers[timer_idx].duty_resolution;
                int duty = (conf->pulse * (1 << duty_resolution)) / conf->precision;
                
                ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_idx, duty);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_idx);
                
                memcpy(&luat_pwm_channels[i].pc, conf, sizeof(luat_pwm_conf_t));
                
                // 如果配置了fade，启动fade过程
                if (conf->fade_time > 0 && conf->target_pulse >= 0) {
                    return luat_pwm_fade(gpio_num, conf->target_pulse, conf->fade_time);
                }
                
                return 0;
            }
            break;
        }
    }
    
    // 检查是否需要限制脉冲值
    if (conf->pulse > conf->precision) {
        conf->pulse = conf->precision;
    }
    
    // 尝试找到一个具有相同配置的定时器
    timer_idx = find_existing_timer(conf);
    
    // 如果没有找到现有定时器，则尝试分配一个新的
    if (timer_idx >= LEDC_TIMER_MAX) {
        timer_idx = find_free_timer();
        if (timer_idx >= LEDC_TIMER_MAX) {
            LLOGE("Too many PWM configurations! Only %d timers supported", PWM_TIMER_COUNT + 1);
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
    
    // 分配或获取对应的LEDC通道号
    channel_idx = find_or_allocate_ledc_channel(gpio_num, timer_idx);
    if (channel_idx >= LEDC_CHANNEL_MAX) {
        LLOGE("Failed to allocate LEDC channel for GPIO %d", gpio_num);
        return -1;
    }
    
    int duty = (conf->pulse * (1 << duty_resolution)) / conf->precision;
    
    // 配置通道
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel_idx,
        .timer_sel = timer_idx,        // 使用定时器索引
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = gpio_num,          // 使用GPIO引脚号
        .duty = duty,
        .hpoint = 0,
    };
    
    ret = ledc_channel_config(&ledc_channel);
    if (ret) {
        LLOGE("ledc_channel_config failed for GPIO %d, channel %d, timer %d: %d", 
              gpio_num, channel_idx, timer_idx, ret);
        // 释放映射
        free_gpio_ledc_mapping(gpio_num);
        return -1;
    }
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_idx, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_idx);
    
    // 更新通道状态
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (luat_pwm_channels[i].ledc_channel == channel_idx) {
            memcpy(&luat_pwm_channels[i].pc, conf, sizeof(luat_pwm_conf_t));
            luat_pwm_channels[i].is_opened = 1;
            luat_pwm_channels[i].bound_timer = timer_idx;
            luat_pwm_channels[i].gpio_num = gpio_num;
            break;
        }
    }
    
    // 如果配置了fade，启动fade过程
    if (conf->fade_time > 0 && conf->target_pulse >= 0) {
        return luat_pwm_fade(gpio_num, conf->target_pulse, conf->fade_time);
    }
    
    return 0;
}

int luat_pwm_fade(int gpio_num, int target_duty, int time_ms) {
    if (gpio_num < 0 || target_duty < 0 || time_ms < 0) {
        LLOGE("Invalid fade parameters: gpio=%d, target_duty=%d, time_ms=%d", 
              gpio_num, target_duty, time_ms);
        return -1;
    }
    
    init_gpio_ledc_map();
    
    // 根据GPIO引脚号查找对应的PWM通道
    int channel_index = -1;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (luat_pwm_channels[i].gpio_num == gpio_num && luat_pwm_channels[i].is_opened) {
            channel_index = i;
            break;
        }
    }
    
    if (channel_index < 0) {
        LLOGE("GPIO %d not allocated for PWM", gpio_num);
        return -1;
    }

    ledc_channel_t channel_idx = luat_pwm_channels[channel_index].ledc_channel;
    ledc_timer_t timer_idx = luat_pwm_channels[channel_index].bound_timer;
    
    // 限制目标占空比
    if (target_duty > luat_pwm_channels[channel_index].pc.precision) {
        target_duty = luat_pwm_channels[channel_index].pc.precision;
    }
    
    // 停止当前的fade过程
    stop_current_fade(timer_idx);
    
    // 安装fade功能
    esp_err_t ret = ledc_fade_func_install(0);  // 优先级0
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) { // ESP_ERR_INVALID_STATE表示已经安装
        LLOGE("ledc_fade_func_install failed: %d", ret);
        return -1;
    }
    
    // 计算目标duty值
    int target_duty_value = (target_duty * (1 << luat_pwm_timers[timer_idx].duty_resolution)) / luat_pwm_channels[channel_index].pc.precision;
    
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
    luat_pwm_channels[channel_index].pc.target_pulse = target_duty;
    
    return 0;
}

int luat_pwm_stop_fade(int gpio_num) {
    if (gpio_num < 0) {
        return -1;
    }
    
    init_gpio_ledc_map();
    
    // 根据GPIO引脚号查找对应的PWM通道
    int channel_index = -1;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (luat_pwm_channels[i].gpio_num == gpio_num && luat_pwm_channels[i].is_opened) {
            channel_index = i;
            break;
        }
    }
    
    if (channel_index < 0) {
        return -1;
    }

    ledc_timer_t timer_idx = luat_pwm_channels[channel_index].bound_timer;
    
    stop_current_fade(timer_idx);
    
    return 0;
}

int luat_pwm_close(int gpio_num){
    if (gpio_num < 0) {
        return -1;
    }
    
    init_gpio_ledc_map();
    
    // 根据GPIO引脚号查找对应的PWM通道
    int channel_index = -1;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (luat_pwm_channels[i].gpio_num == gpio_num && luat_pwm_channels[i].is_opened) {
            channel_index = i;
            break;
        }
    }
    
    if (channel_index < 0) {
        return -1;
    }

    ledc_channel_t channel_idx = luat_pwm_channels[channel_index].ledc_channel;
    ledc_timer_t timer_idx = luat_pwm_channels[channel_index].bound_timer;
    
    // 停止fade
    stop_current_fade(timer_idx);
    
    int ret = ledc_stop(LEDC_LOW_SPEED_MODE, channel_idx, 0);
    if (ret) {
        LLOGE("ledc_stop failed: %d", ret);
        return -1;
    }
    
    gpio_reset_pin(gpio_num);
    
    // 清除通道状态
    luat_pwm_channels[channel_index].is_opened = 0;
    memset(&luat_pwm_channels[channel_index].pc, 0, sizeof(luat_pwm_conf_t));
    luat_pwm_channels[channel_index].bound_timer = -1;
    luat_pwm_channels[channel_index].gpio_num = -1;
    
    // 清理GPIO到LEDC通道的映射
    free_gpio_ledc_mapping(gpio_num);
    
    // 检查是否有其他通道正在使用此定时器
    int other_channels_using_timer = 0;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
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