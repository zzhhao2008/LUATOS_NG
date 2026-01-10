# LEDC PWM模块文档

**当前版本：V1008_2**  
**适用平台：ESP32S3**  
**最后更新：2026年1月10日**

## 模块概述

LEDC PWM模块提供了基于ESP32S3 LEDC控制器的PWM（脉冲宽度调制）功能支持。本模块支持：
- 多通道PWM输出
- 频率范围：1Hz - 13MHz
- 硬件加速的线性占空比过渡（fade）
- 多种分频精度（100/256/1000）
- 精确的时间控制

本模块特别适合用于LED背光调节、电机控制、音频生成等需要精确PWM输出的场景。

## API函数说明

### 1. `pwm.open(channel, period, pulse, pnum, precision, fade_time, target_pulse)`

**功能**：开启指定的PWM通道

**参数**：
- `channel` (int)：PWM通道号（实际对应GPIO引脚）
- `period` (int)：频率，单位Hz (1-1000000)
- `pulse` (int)：初始占空比 (0-分频精度)
- `pnum` (int, 可选)：输出周期数，0为持续输出，1为单次输出，默认0
- `precision` (int, 可选)：分频精度，100/256/1000，默认为100
- `fade_time` (int, 可选)：过渡时间(毫秒)，0表示不使用平滑过渡，默认0
- `target_pulse` (int, 可选)：目标占空比，仅当fade_time>0时有效，默认-1

**返回值**：
- `boolean`：处理结果，成功返回true，失败返回false

**示例**：
```lua
-- 基本用法：打开PWM5，频率1kHz，占空比50%
pwm.open(5, 1000, 50)

-- 高级用法：打开PWM18，频率10kHz，占空比31/256，持续输出
pwm.open(18, 10000, 31, 0, 256)

-- 带fade功能：打开PWM8，频率1kHz，从10%占空比过渡到80%，耗时2000ms
pwm.open(8, 1000, 10, 0, 100, 2000, 80)
```

### 2. `pwm.fade(channel, target_duty, time_ms)`

**功能**：设置PWM占空比平滑过渡

**参数**：
- `channel` (int)：PWM通道号
- `target_duty` (int)：目标占空比 (0-分频精度)
- `time_ms` (int)：过渡时间(毫秒)

**返回值**：
- `boolean`：处理结果，成功返回true，失败返回false

**示例**：
```lua
-- PWM5从当前占空比平滑过渡到80%，耗时1000ms
pwm.fade(5, 80, 1000)

-- 背光渐亮效果
pwm.open(8, 1000, 0)  -- 初始0%占空比
pwm.fade(8, 100, 3000)  -- 3秒内渐亮到100%
```

### 3. `pwm.stop_fade(channel)`

**功能**：停止PWM占空比过渡

**参数**：
- `channel` (int)：PWM通道号

**返回值**：
- `boolean`：处理结果，成功返回true，失败返回false

**示例**：
```lua
-- 停止PWM5的过渡
pwm.stop_fade(5)

-- 500ms后停止渐亮效果
pwm.open(8, 1000, 0)
pwm.fade(8, 100, 3000)
sys.timerStart(function()
    pwm.stop_fade(8)
    log.info("fade", "stopped at 500ms")
end, 500)
```

### 4. `pwm.close(channel)`

**功能**：关闭指定的PWM通道

**参数**：
- `channel` (int)：PWM通道号

**返回值**：
- `nil`：无返回值

**示例**：
```lua
-- 关闭PWM5
pwm.close(5)
```

### 5. `pwm.capture(channel, freq)`

**功能**：PWM捕获（当前版本暂未实现）

**参数**：
- `channel` (int)：PWM通道号
- `freq` (int)：捕获频率

**返回值**：
- `boolean`：处理结果，成功返回true，失败返回false

**示例**：
```lua
-- 注意：当前版本此功能未实现
local ret = pwm.capture(0, 1000)
```

## 使用示例

### 1. 基础LED控制
```lua
-- 配置背光引脚（根据实际硬件修改）
local BACKLIGHT_PIN = 8

-- 打开背光PWM，频率1kHz，初始亮度50%
if pwm.open(BACKLIGHT_PIN, 1000, 50) then
    log.info("backlight", "initialized successfully")
else
    log.error("backlight", "failed to initialize")
end

-- 3秒后调整到75%亮度
sys.timerStart(function()
    pwm.open(BACKLIGHT_PIN, 1000, 75)
    log.info("backlight", "brightness adjusted to 75%")
end, 3000)
```

### 2. 平滑亮度过渡（呼吸灯效果）
```lua
-- 配置LED引脚
local LED_PIN = 5

-- 初始化PWM，频率100Hz，初始亮度0%
pwm.open(LED_PIN, 100, 0)

-- 呼吸灯效果
local function breath_light()
    -- 从0%到100%，耗时2秒
    pwm.fade(LED_PIN, 100, 2000)
    
    -- 等待2秒
    sys.wait(2000)
    
    -- 从100%到0%，耗时2秒
    pwm.fade(LED_PIN, 0, 2000)
    
    -- 等待2秒
    sys.wait(2000)
end

-- 循环执行
while true do
    breath_light()
end
```

### 3. 触屏时钟背光调节
```lua
-- 背光PWM配置
local LCD_BL_PIN = 8
local CURRENT_BRIGHTNESS = 50  -- 当前亮度(0-100)

-- 初始化背光
function init_backlight()
    pwm.open(LCD_BL_PIN, 1000, CURRENT_BRIGHTNESS)
    log.info("backlight", "initialized at "..CURRENT_BRIGHTNESS.."%")
end

-- 设置亮度（带平滑过渡）
function set_brightness(level, fade_time)
    -- 限制亮度范围
    level = math.max(0, math.min(100, level))
    fade_time = fade_time or 500  -- 默认500ms过渡时间
    
    log.info("backlight", string.format("changing from %d%% to %d%% over %dms", 
        CURRENT_BRIGHTNESS, level, fade_time))
    
    pwm.fade(LCD_BL_PIN, level, fade_time)
    CURRENT_BRIGHTNESS = level
end

-- 自动亮度调节（根据时间）
function auto_brightness_adjust()
    while true do
        local hour = os.date("%H")
        local target_brightness
        
        if hour >= 6 and hour < 8 then
            -- 早晨，逐渐变亮
            target_brightness = 30 + (hour - 6) * 35
        elseif hour >= 8 and hour < 20 then
            -- 白天，全亮
            target_brightness = 100
        elseif hour >= 20 and hour < 22 then
            -- 晚上，逐渐变暗
            target_brightness = 100 - (hour - 20) * 35
        else
            -- 夜间，微亮
            target_brightness = 10
        end
        
        set_brightness(target_brightness, 30000)  -- 30秒平滑过渡
        sys.wait(60000)  -- 每分钟检查一次
    end
end

-- 启动背光控制
init_backlight()
sys.taskInit(auto_brightness_adjust)

-- 触摸事件调整亮度（示例）
screen.onEvent(function(event)
    if event.type == "TOUCH" then
        -- 双击屏幕增加亮度
        if event.gesture == "DOUBLE_CLICK" then
            local new_brightness = math.min(100, CURRENT_BRIGHTNESS + 20)
            set_brightness(new_brightness, 300)
        end
    end
end)
```

### 4. 电机控制示例
```lua
-- 电机PWM配置
local MOTOR_PIN = 6
local MOTOR_FREQ = 20000  -- 20kHz

-- 初始化电机PWM
function init_motor()
    pwm.open(MOTOR_PIN, MOTOR_FREQ, 0)  -- 初始停止
    log.info("motor", "initialized")
end

-- 设置电机速度 (0-100%)
function set_motor_speed(speed, fade_time)
    speed = math.max(0, math.min(100, speed))
    fade_time = fade_time or 1000  -- 默认1秒过渡
    
    log.info("motor", "setting speed to "..speed.."%")
    pwm.fade(MOTOR_PIN, speed, fade_time)
end

-- 电机控制任务
function motor_control_task()
    init_motor()
    
    -- 启动序列：0% -> 30% -> 60% -> 100%
    local speeds = {30, 60, 100}
    local times = {2000, 3000, 5000}
    
    for i = 1, #speeds do
        set_motor_speed(speeds[i], times[i])
        sys.wait(times[i] + 500)  -- 等待过渡完成
    end
    
    -- 持续运行10秒
    sys.wait(10000)
    
    -- 停止序列：100% -> 50% -> 0%
    local stop_speeds = {50, 0}
    local stop_times = {2000, 3000}
    
    for i = 1, #stop_speeds do
        set_motor_speed(stop_speeds[i], stop_times[i])
        sys.wait(stop_times[i] + 500)
    end
    
    log.info("motor", "control sequence completed")
end

sys.taskInit(motor_control_task)
```


## 版本信息

**当前版本：V1008_2**

**版本特性**：
- ✅ 完整支持ESP32S3 LEDC控制器
- ✅ 硬件加速的线性占空比过渡（fade）
- ✅ 多通道独立控制
- ✅ 高精度频率和占空比设置
- ✅ 低CPU占用率


**注意**：本模块处于持续开发中，API可能在后续版本中有所调整，请及时关注版本更新说明。