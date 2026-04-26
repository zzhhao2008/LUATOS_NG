-- ES8311音频编解码器示例
-- 演示ES8311的基本使用，包括播放、录制以及与ft6336共享I2C总线

local log = require "log"
log.level = log.LOG_LEVEL_INFO

log.info("es8311_example", "Starting ES8311 example")

-- 导入ES8311驱动
local es8311 = require "es8311"

-- 导入ft6336触摸屏驱动(可选，用于演示I2C总线共享)
local ft6336 = pcall(require, "ft6336")
if ft6336 then
    log.info("es8311_example", "ft6336 loaded for I2C bus sharing demo")
else
    log.info("es8311_example", "ft6336 not available")
    ft6336 = nil
end

-- 示例1: 基本播放功能
local function example_playback()
    log.info("es8311_example", "=== Example 1: Audio Playback ===")

    -- 初始化ES8311
    local success = es8311.init({
        sda = 10,          -- SDA引脚
        scl = 9,           -- SCL引脚
        mclk = 4,          -- MCLK引脚
        sclk = 5,          -- SCLK引脚(I2S BCLK)
        asdout = 46,       -- ASDOUT引脚(I2S DAC输出)
        lrck = 12,         -- LRCK引脚(I2S LRCK)
        dsin = 45,         -- DSIN引脚(I2S ADC输入)
        i2c_id = 0,        -- I2C端口号
        fre = 100000       -- I2C频率
    })

    if not success then
        log.error("es8311_example", "Failed to initialize ES8311")
        return false
    end

    log.info("es8311_example", "ES8311 initialized successfully")

    -- 获取芯片信息
    local info = es8311.get_info()
    if info then
        log.info("es8311_example", "Chip ID:", info.chip_id)
        log.info("es8311_example", "Version:", info.version)
    end

    -- 设置播放参数
    es8311.set_mode(es8311.MODE_DAC)
    es8311.set_sample_rate(44100)
    es8311.set_volume(80)  -- 80%音量

    log.info("es8311_example", "Audio mode set, volume 80%")

    -- 开始播放
    success = es8311.start(es8311.MODE_DAC)
    if not success then
        log.error("es8311_example", "Failed to start playback")
        return false
    end

    log.info("es8311_example", "Playback started")

    -- 模拟音频数据(实际应用中从文件读取或生成)
    -- 这里只是演示，实际需要真实的音频数据
    local sample_rate = 44100
    local duration = 1  -- 1秒
    local num_samples = sample_rate * duration * 2  -- 立体声，2字节每样本
    local audio_data = string.rep("\x00", num_samples)  -- 静音数据作为示例

    log.info("es8311_example", "Writing audio data...")
    local written = es8311.write(audio_data, #audio_data)
    log.info("es8311_example", "Wrote", written, "bytes")

    -- 延迟让播放完成
    log.info("es8311_example", "Waiting for playback to complete...")
    sys.wait(2000)  -- 等待2秒

    -- 停止播放
    es8311.stop()
    log.info("es8311_example", "Playback stopped")

    return true
end

-- 示例2: 基本录制功能
local function example_recording()
    log.info("es8311_example", "=== Example 2: Audio Recording ===")

    -- 如果ES8311已经初始化，先停止
    es8311.stop()

    -- 设置录制参数
    es8311.set_mode(es8311.MODE_ADC)
    es8311.set_sample_rate(16000)  -- 16kHz采样率

    log.info("es8311_example", "Audio mode set to recording, 16kHz")

    -- 开始录制
    local success = es8311.start(es8311.MODE_ADC)
    if not success then
        log.error("es8311_example", "Failed to start recording")
        return false
    end

    log.info("es8311_example", "Recording started")

    -- 录制2秒音频
    local sample_rate = 16000
    local duration = 2  -- 2秒
    local total_samples = sample_rate * duration * 2  -- 立体声，2字节每样本
    local buffer_size = 1024  -- 每次读取1024字节
    local total_received = 0

    log.info("es8311_example", "Recording for", duration, "seconds...")

    local start_time = sys.tick()
    while (sys.tick() - start_time) < duration * 1000 do
        local data = es8311.read(buffer_size)
        if data then
            total_received = total_received + #data
            log.info("es8311_example", "Received", #data, "bytes, total:", total_received)
        end
        sys.wait(50)  -- 短暂延迟
    end

    log.info("es8311_example", "Recording completed, total received:", total_received, "bytes")

    -- 停止录制
    es8311.stop()
    log.info("es8311_example", "Recording stopped")

    return true
end

-- 示例3: 与ft6336共享I2C总线
local function example_i2c_sharing()
    log.info("es8311_example", "=== Example 3: I2C Bus Sharing with FT6336 ===")

    if not ft6336 then
        log.warn("es8311_example", "FT6336 not available, skipping I2C sharing example")
        return true
    end

    -- 初始化ft6336触摸屏
    local ft6336_success = ft6336.init({
        sda = 10,        -- 与ES8311使用相同的I2C引脚
        scl = 9,         -- 与ES8311使用相同的I2C引脚
        i2c_id = 0,      -- 与ES8311使用相同的I2C端口号
        fre = 100000,    -- 与ES8311使用相同的I2C频率
        x_limit = 320,
        y_limit = 240
    })

    if not ft6336_success then
        log.error("es8311_example", "Failed to initialize FT6336")
        return false
    end

    log.info("es8311_example", "FT6336 initialized successfully")

    -- 重新初始化ES8311（实际应用中应该保持之前的状态）
    local es8311_success = es8311.init({
        sda = 10,
        scl = 9,
        i2c_id = 0,
        fre = 100000
    })

    if not es8311_success then
        log.error("es8311_example", "Failed to re-initialize ES8311")
        return false
    end

    log.info("es8311_example", "Both devices sharing I2C bus successfully")

    -- 演示同时使用两个设备
    es8311.set_mode(es8311.MODE_DAC)
    es8311.set_volume(50)
    es8311.start(es8311.MODE_DAC)

    log.info("es8311_example", "ES8311 playback started")

    -- 读取触摸屏数据
    for i = 1, 5 do
        local x, y, state = ft6336.read()
        log.info("es8311_example", "FT6336:", "x=", x, "y=", y, "state=", state)
        sys.wait(500)

        -- 同时写入一些音频数据
        local dummy_data = string.rep("\x00", 512)
        es8311.write(dummy_data, #dummy_data)
    end

    log.info("es8311_example", "I2C bus sharing test completed")

    es8311.stop()
    return true
end

-- 主函数
local function main()
    log.info("es8311_example", "ES8311 Audio Codec Example")
    log.info("es8311_example", "================================")

    -- 运行示例1: 播放
    if example_playback() then
        log.info("es8311_example", "Playback example completed")
    else
        log.error("es8311_example", "Playback example failed")
    end

    sys.wait(1000)

    -- 运行示例2: 录制
    if example_recording() then
        log.info("es8311_example", "Recording example completed")
    else
        log.error("es8311_example", "Recording example failed")
    end

    sys.wait(1000)

    -- 运行示例3: I2C总线共享
    if example_i2c_sharing() then
        log.info("es8311_example", "I2C sharing example completed")
    else
        log.error("es8311_example", "I2C sharing example failed")
    end

    log.info("es8311_example", "All examples completed")
end

-- 启动主函数
sys.taskInit(main)

log.info("es8311_example", "Example script loaded")
