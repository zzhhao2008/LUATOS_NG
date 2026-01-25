#include "luat_base.h"
#include "luat_pwm.h"
#include <math.h>
#include "driver/ledc.h"
#include "esp_timer.h"
#include "luat_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#define LUAT_LOG_TAG "pwm"

// 定义定时器和通道数量
#ifdef CONFIG_IDF_TARGET_ESP32S3
    #define PWM_TIMER_COUNT LEDC_TIMER_3  // ESP32-S3: 4 timers (0-3)
    #define PWM_CHANNEL_COUNT LEDC_CHANNEL_7  // ESP32-S3: 8 channels (0-7)
#elif CONFIG_IDF_TARGET_ESP32C3
    #define PWM_TIMER_COUNT LEDC_TIMER_1  // ESP32-C3: 2 timers (0-1)
    #define PWM_CHANNEL_COUNT LEDC_CHANNEL_3  // ESP32-C3: 4 channels (0-3)
#else  // ESP32
    #define PWM_TIMER_COUNT LEDC_TIMER_3  // ESP32: 4 timers (0-3)
    #define PWM_CHANNEL_COUNT LEDC_CHANNEL_7  // ESP32: 8 channels (0-7)
#endif

// 通道状态结构（增强版，支持软件fade）
typedef struct {
    int gpio_num;                // GPIO引脚号
    ledc_channel_t ledc_channel; // LEDC硬件通道
    ledc_timer_t bound_timer;    // 绑定的定时器
    
    uint8_t is_opened;           // 是否开启
    uint8_t is_fading;           // 是否正在fade
    uint8_t is_hardware_fading;  // 是否使用硬件fade
    
    int current_duty;            // 当前占空比（原始值）
    int target_duty;             // 目标占空比（原始值）
    int start_duty;              // fade起始占空比
    uint32_t fade_start_time;    // fade开始时间(ms)
    uint32_t fade_duration;      // fade持续时间(ms)
    
    int precision;               // 精度（分母）
    int duty_resolution;         // 定时器分辨率
    
    luat_pwm_conf_t config;      // 原始配置
} pwm_channel_state_t;

static pwm_channel_state_t pwm_channels[PWM_CHANNEL_COUNT + 1];
static TimerHandle_t software_fade_timer = NULL;
static bool fade_timer_running = false;
static esp_timer_handle_t hardware_fade_timer = NULL;

// 定时器状态
typedef struct {
    uint8_t is_used;
    int frequency;
    int duty_resolution;
} pwm_timer_state_t;

static pwm_timer_state_t pwm_timers[PWM_TIMER_COUNT + 1];

// GPIO到通道映射
static int gpio_to_channel_map[34] = { -1 }; // ESP32最多34个GPIO

// 硬件抽象函数
static inline uint32_t ilog2(uint32_t i) {
    assert(i > 0);
    uint32_t log = 0;
    while (i >>= 1) {
        ++log;
    }
    return log;
}

static uint32_t calculate_duty_resolution(uint32_t frequency) {
    const uint32_t apb_clk = 80000000; // 80MHz
    uint32_t div = (apb_clk + frequency / 2) / frequency;
    uint32_t resolution = ilog2(div);
    
    if (resolution > SOC_LEDC_TIMER_BIT_WIDTH - 1) {
        resolution = SOC_LEDC_TIMER_BIT_WIDTH - 1;
    }
    
    return resolution;
}

// 初始化通道状态
static void init_pwm_system(void) {
    static bool initialized = false;
    if (initialized) return;
    
    // 初始化通道状态
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        memset(&pwm_channels[i], 0, sizeof(pwm_channel_state_t));
        pwm_channels[i].gpio_num = -1;
        pwm_channels[i].ledc_channel = i;
        pwm_channels[i].bound_timer = -1;
    }
    
    // 初始化定时器状态
    for (int i = 0; i <= PWM_TIMER_COUNT; i++) {
        memset(&pwm_timers[i], 0, sizeof(pwm_timer_state_t));
    }
    
    // 初始化GPIO映射
    for (int i = 0; i < 34; i++) {
        gpio_to_channel_map[i] = -1;
    }
    
    initialized = true;
}

// 查找或分配定时器
static ledc_timer_t allocate_timer(int frequency) {
    // 先找相同频率的定时器
    for (int i = 0; i <= PWM_TIMER_COUNT; i++) {
        if (pwm_timers[i].is_used && pwm_timers[i].frequency == frequency) {
            return i;
        }
    }
    
    // 找空闲定时器
    for (int i = 0; i <= PWM_TIMER_COUNT; i++) {
        if (!pwm_timers[i].is_used) {
            pwm_timers[i].is_used = 1;
            pwm_timers[i].frequency = frequency;
            pwm_timers[i].duty_resolution = calculate_duty_resolution(frequency);
            
            // 配置定时器
            ledc_timer_config_t timer_config = {
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .timer_num = i,
                .freq_hz = frequency,
                .clk_cfg = LEDC_AUTO_CLK,
                .duty_resolution = pwm_timers[i].duty_resolution
            };
            
            if (ledc_timer_config(&timer_config) != ESP_OK) {
                pwm_timers[i].is_used = 0;
                return LEDC_TIMER_MAX;
            }
            
            return i;
        }
    }
    
    return LEDC_TIMER_MAX;
}

// 释放定时器（如果无其他通道使用）
static void release_timer(ledc_timer_t timer_idx) {
    if (timer_idx > PWM_TIMER_COUNT) return;
    
    bool used_by_other = false;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (pwm_channels[i].is_opened && pwm_channels[i].bound_timer == timer_idx) {
            used_by_other = true;
            break;
        }
    }
    
    if (!used_by_other) {
        pwm_timers[timer_idx].is_used = 0;
        // 注意：不调用ledc_timer_rst，因为可能影响其他通道
    }
}

// 查找或分配通道
static int allocate_channel(int gpio_num) {
    if (gpio_num < 0 || gpio_num >= 34) {
        LLOGE("Invalid GPIO number: %d", gpio_num);
        return -1;
    }
    
    // 检查GPIO是否已被分配
    if (gpio_to_channel_map[gpio_num] >= 0) {
        return gpio_to_channel_map[gpio_num];
    }
    
    // 寻找空闲通道
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (!pwm_channels[i].is_opened) {
            gpio_to_channel_map[gpio_num] = i;
            pwm_channels[i].gpio_num = gpio_num;
            pwm_channels[i].ledc_channel = i;
            return i;
        }
    }
    
    LLOGE("No free PWM channels available");
    return -1;
}

// 释放通道
static void release_channel(int channel_idx) {
    if (channel_idx < 0 || channel_idx > PWM_CHANNEL_COUNT) return;
    
    if (pwm_channels[channel_idx].gpio_num >= 0) {
        gpio_to_channel_map[pwm_channels[channel_idx].gpio_num] = -1;
    }
    
    memset(&pwm_channels[channel_idx], 0, sizeof(pwm_channel_state_t));
    pwm_channels[channel_idx].gpio_num = -1;
    pwm_channels[channel_idx].ledc_channel = channel_idx;
}

// 设置通道占空比（软件计算）
static void set_channel_duty(int channel_idx, int duty_value) {
    if (channel_idx < 0 || channel_idx > PWM_CHANNEL_COUNT) return;
    
    pwm_channel_state_t *ch = &pwm_channels[channel_idx];
    if (!ch->is_opened) return;
    
    // 计算实际的duty值
    int max_duty = (1 << ch->duty_resolution);
    if (duty_value > max_duty) duty_value = max_duty;
    if (duty_value < 0) duty_value = 0;
    
    ch->current_duty = duty_value;
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ch->ledc_channel, duty_value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ch->ledc_channel);
}

// 软件fade定时器回调
static void software_fade_timer_callback(TimerHandle_t xTimer) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool any_fading = false;
    
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        pwm_channel_state_t *ch = &pwm_channels[i];
        
        if (!ch->is_opened || !ch->is_fading || ch->is_hardware_fading) {
            continue;
        }
        
        any_fading = true;
        
        // 计算fade进度
        uint32_t elapsed = current_time - ch->fade_start_time;
        if (elapsed >= ch->fade_duration) {
            // fade完成
            ch->current_duty = ch->target_duty;
            ch->is_fading = false;
            set_channel_duty(i, ch->target_duty);
        } else {
            // 线性插值计算当前占空比
            float progress = (float)elapsed / (float)ch->fade_duration;
            int current_duty = ch->start_duty + (int)((ch->target_duty - ch->start_duty) * progress);
            
            // 防止超限
            if (current_duty > ch->target_duty && ch->start_duty > ch->target_duty) {
                current_duty = ch->target_duty;
            } else if (current_duty < ch->target_duty && ch->start_duty < ch->target_duty) {
                current_duty = ch->target_duty;
            }
            
            set_channel_duty(i, current_duty);
        }
    }
    
    // 如果没有通道在fade，停止定时器
    if (!any_fading && fade_timer_running) {
        xTimerStop(software_fade_timer, 0);
        fade_timer_running = false;
    }
}

// 初始化软件fade定时器
static void init_software_fade_timer(void) {
    if (software_fade_timer == NULL) {
        software_fade_timer = xTimerCreate("sw_fade_timer", 
                                          pdMS_TO_TICKS(10),  // 10ms周期
                                          pdTRUE,            // 自动重载
                                          (void *)0, 
                                          software_fade_timer_callback);
    }
}

// 启动软件fade
static int start_software_fade(int channel_idx, int target_duty, int duration_ms) {
    if (channel_idx < 0 || channel_idx > PWM_CHANNEL_COUNT) return -1;
    
    pwm_channel_state_t *ch = &pwm_channels[channel_idx];
    if (!ch->is_opened) return -1;
    
    // 停止当前fade
    ch->is_fading = false;
    
    // 计算原始duty值
    int max_value = (1 << ch->duty_resolution);
    int current_raw = ch->current_duty;
    int target_raw = (target_duty * max_value) / ch->precision;
    
    // 限制目标值
    if (target_raw > max_value) target_raw = max_value;
    if (target_raw < 0) target_raw = 0;
    
    // 设置fade参数
    ch->start_duty = current_raw;
    ch->target_duty = target_raw;
    ch->fade_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ch->fade_duration = duration_ms;
    ch->is_fading = true;
    ch->is_hardware_fading = false;
    
    // 启动定时器
    if (!fade_timer_running) {
        xTimerStart(software_fade_timer, 0);
        fade_timer_running = true;
    }
    
    return 0;
}

// 启动硬件fade（单通道）
static int start_hardware_fade(int channel_idx, int target_duty, int duration_ms) {
    if (channel_idx < 0 || channel_idx > PWM_CHANNEL_COUNT) return -1;
    
    pwm_channel_state_t *ch = &pwm_channels[channel_idx];
    if (!ch->is_opened) return -1;
    
    // 计算原始duty值
    int max_value = (1 << ch->duty_resolution);
    int target_raw = (target_duty * max_value) / ch->precision;
    
    if (target_raw > max_value) target_raw = max_value;
    if (target_raw < 0) target_raw = 0;
    
    // 安装fade功能（如果未安装）
    static bool fade_installed = false;
    if (!fade_installed) {
        esp_err_t ret = ledc_fade_func_install(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            LLOGE("ledc_fade_func_install failed: %d", ret);
            return -1;
        }
        fade_installed = true;
    }
    
    // 设置fade
    esp_err_t ret = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, 
                                          ch->ledc_channel, 
                                          target_raw, 
                                          duration_ms);
    if (ret != ESP_OK) {
        LLOGE("ledc_set_fade_with_time failed: %d", ret);
        return -1;
    }
    
    // 启动fade
    ret = ledc_fade_start(LEDC_LOW_SPEED_MODE, ch->ledc_channel, LEDC_FADE_NO_WAIT);
    if (ret != ESP_OK) {
        LLOGE("ledc_fade_start failed: %d", ret);
        return -1;
    }
    
    // 更新状态
    ch->target_duty = target_raw;
    ch->is_fading = true;
    ch->is_hardware_fading = true;
    
    return 0;
}

// API实现
int luat_pwm_setup(luat_pwm_conf_t *conf) {
    init_pwm_system();
    init_software_fade_timer();
    
    if (!conf) {
        LLOGE("NULL config pointer");
        return -1;
    }
    
    int gpio_num = conf->channel;
    int frequency = conf->period;
    int initial_duty = conf->pulse;
    int precision = conf->precision;
    
    // 设置默认precision（100表示百分比）
    if (precision <= 0) precision = 100;
    
    // 分配通道
    int channel_idx = allocate_channel(gpio_num);
    if (channel_idx < 0) {
        LLOGE("Failed to allocate channel for GPIO %d", gpio_num);
        return -1;
    }
    
    pwm_channel_state_t *ch = &pwm_channels[channel_idx];
    
    // 如果通道已打开，先关闭
    if (ch->is_opened) {
        luat_pwm_close(gpio_num);
        // 重新分配
        channel_idx = allocate_channel(gpio_num);
        if (channel_idx < 0) return -1;
        ch = &pwm_channels[channel_idx];
    }
    
    // 分配定时器
    ledc_timer_t timer_idx = allocate_timer(frequency);
    if (timer_idx > PWM_TIMER_COUNT) {
        LLOGE("Failed to allocate timer for frequency %d", frequency);
        release_channel(channel_idx);
        return -1;
    }
    
    // 配置通道
    int max_value = (1 << pwm_timers[timer_idx].duty_resolution);
    int initial_duty_raw = (initial_duty * max_value) / precision;
    
    if (initial_duty_raw > max_value) initial_duty_raw = max_value;
    if (initial_duty_raw < 0) initial_duty_raw = 0;
    
    ledc_channel_config_t channel_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel_idx,
        .timer_sel = timer_idx,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = gpio_num,
        .duty = initial_duty_raw,
        .hpoint = 0
    };
    
    if (ledc_channel_config(&channel_config) != ESP_OK) {
        LLOGE("ledc_channel_config failed for GPIO %d", gpio_num);
        release_timer(timer_idx);
        release_channel(channel_idx);
        return -1;
    }
    
    // 更新通道状态
    ch->is_opened = 1;
    ch->bound_timer = timer_idx;
    ch->duty_resolution = pwm_timers[timer_idx].duty_resolution;
    ch->precision = precision;
    ch->current_duty = initial_duty_raw;
    ch->target_duty = initial_duty_raw;
    ch->is_fading = false;
    ch->is_hardware_fading = false;
    
    memcpy(&ch->config, conf, sizeof(luat_pwm_conf_t));
    
    // 如果有fade配置，启动fade
    if (conf->fade_time > 0 && conf->target_pulse >= 0) {
        return luat_pwm_fade(gpio_num, conf->target_pulse, conf->fade_time);
    }
    
    return 0;
}

int luat_pwm_fade(int gpio_num, int target_duty, int duration_ms) {
    init_pwm_system();
    init_software_fade_timer();
    
    if (gpio_num < 0 || target_duty < 0 || duration_ms <= 0) {
        LLOGE("Invalid fade parameters: gpio=%d, target=%d, duration=%d", 
              gpio_num, target_duty, duration_ms);
        return -1;
    }
    
    // 查找通道
    int channel_idx = -1;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (pwm_channels[i].gpio_num == gpio_num && pwm_channels[i].is_opened) {
            channel_idx = i;
            break;
        }
    }
    
    if (channel_idx < 0) {
        LLOGE("GPIO %d not configured for PWM", gpio_num);
        return -1;
    }
    
    // 检查是否所有通道都在使用硬件fade
    bool all_hardware_fading = true;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (pwm_channels[i].is_opened && pwm_channels[i].is_fading && !pwm_channels[i].is_hardware_fading) {
            all_hardware_fading = false;
            break;
        }
    }
    
    // 策略：如果有其他通道在软件fade，当前通道也用软件fade
    // 否则，尝试使用硬件fade
    pwm_channel_state_t *ch = &pwm_channels[channel_idx];
    
    if (ch->is_fading && ch->is_hardware_fading) {
        // 当前通道正在硬件fade，停止它
        ledc_fade_stop(LEDC_LOW_SPEED_MODE, ch->ledc_channel);
        ch->is_fading = false;
    }
    
    // 检查是否有其他通道在硬件fade
    bool other_hardware_fading = false;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (i != channel_idx && pwm_channels[i].is_opened && 
            pwm_channels[i].is_fading && pwm_channels[i].is_hardware_fading) {
            other_hardware_fading = true;
            break;
        }
    }
    
    int result = -1;
    if (other_hardware_fading || all_hardware_fading) {
        // 有其他通道在硬件fade，使用软件fade
        result = start_software_fade(channel_idx, target_duty, duration_ms);
    } else {
        // 尝试硬件fade
        result = start_hardware_fade(channel_idx, target_duty, duration_ms);
        
        // 如果硬件fade失败（比如资源不足），回退到软件fade
        if (result != 0) {
            LLOGW("Hardware fade failed, falling back to software fade");
            result = start_software_fade(channel_idx, target_duty, duration_ms);
        }
    }
    
    return result;
}

int luat_pwm_stop_fade(int gpio_num) {
    init_pwm_system();
    
    if (gpio_num < 0) return -1;
    
    // 查找通道
    int channel_idx = -1;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (pwm_channels[i].gpio_num == gpio_num && pwm_channels[i].is_opened) {
            channel_idx = i;
            break;
        }
    }
    
    if (channel_idx < 0) return -1;
    
    pwm_channel_state_t *ch = &pwm_channels[channel_idx];
    
    if (ch->is_fading) {
        if (ch->is_hardware_fading) {
            ledc_fade_stop(LEDC_LOW_SPEED_MODE, ch->ledc_channel);
        }
        ch->is_fading = false;
    }
    
    return 0;
}

int luat_pwm_close(int gpio_num) {
    init_pwm_system();
    
    if (gpio_num < 0) return -1;
    
    // 查找通道
    int channel_idx = -1;
    for (int i = 0; i <= PWM_CHANNEL_COUNT; i++) {
        if (pwm_channels[i].gpio_num == gpio_num && pwm_channels[i].is_opened) {
            channel_idx = i;
            break;
        }
    }
    
    if (channel_idx < 0) return -1;
    
    pwm_channel_state_t *ch = &pwm_channels[channel_idx];
    
    // 停止fade
    luat_pwm_stop_fade(gpio_num);
    
    // 停止PWM
    ledc_stop(LEDC_LOW_SPEED_MODE, ch->ledc_channel, 0);
    
    // 重置GPIO
    gpio_reset_pin(gpio_num);
    
    // 释放定时器
    ledc_timer_t timer_idx = ch->bound_timer;
    release_channel(channel_idx);
    release_timer(timer_idx);
    
    return 0;
}

int luat_pwm_capture(int channel, int freq) {
    return -1; // 不支持
}