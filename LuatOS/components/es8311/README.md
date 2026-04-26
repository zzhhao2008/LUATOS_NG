# ES8311 音频编解码器驱动

ES8311是高性能I2S音频编解码器，支持音频播放和录制功能。本驱动实现了ES8311的完整功能，包括I2C总线共享支持，可以与ft6336等其他I2C设备共享同一总线。

## 硬件连接

### ESP32S3 默认引脚配置

| 引脚    | ESP32S3 GPIO | 功能描述        |
|---------|--------------|-----------------|
| SCL     | 9            | I2C时钟线      |
| SDA     | 10           | I2C数据线      |
| MCLK    | 4            | 主时钟输出      |
| SCLK    | 5            | I2S位时钟      |
| ASDOUT  | 46           | I2S DAC输出    |
| LRCK    | 12           | I2S左右声道时钟|
| DSIN    | 45           | I2S ADC输入    |

## 功能特性

- ✅ I2C总线共享：与ft6336等其他I2C设备共享同一总线
- ✅ 音频播放：通过I2S接口播放音频数据
- ✅ 音频录制：通过I2S接口录制音频数据
- ✅ 音量控制：支持0-100%音量调节
- ✅ 静音功能：支持播放静音
- ✅ 采样率设置：支持8k/16k/32k/44.1k/48kHz
- ✅ 多种模式：DAC播放、ADC录制、同时播放和录制

## 基本使用

### 1. 初始化ES8311

```lua
local es8311 = require "es8311"

local success = es8311.init({
    sda = 10,          -- SDA引脚
    scl = 9,           -- SCL引脚
    mclk = 4,          -- MCLK引脚
    sclk = 5,          -- SCLK引脚(I2S BCLK)
    asdout = 46,       -- ASDOUT引脚(I2S DAC输出)
    lrck = 12,         -- LRCK引脚(I2S LRCK)
    dsin = 45,         -- DSIN引脚(I2S ADC输入)
    i2c_id = 0,        -- I2C端口号(与ft6336共享)
    fre = 100000       -- I2C频率(与ft6336保持一致)
})

if not success then
    log.error("es8311", "init failed")
    return
end
```

### 2. 音频播放

```lua
-- 设置为播放模式
es8311.set_mode(es8311.MODE_DAC)

-- 设置采样率
es8311.set_sample_rate(44100)

-- 设置音量(0-100)
es8311.set_volume(80)

-- 开始播放
es8311.start(es8311.MODE_DAC)

-- 写入音频数据
local audio_data = ... -- 音频数据
es8311.write(audio_data, #audio_data)

-- ... 继续写入音频数据 ...

-- 停止播放
es8311.stop()
```

### 3. 音频录制

```lua
-- 设置为录制模式
es8311.set_mode(es8311.MODE_ADC)

-- 设置采样率
es8311.set_sample_rate(16000)

-- 开始录制
es8311.start(es8311.MODE_ADC)

-- 读取音频数据
while recording do
    local data = es8311.read(1024)  -- 每次读取1024字节
    if data then
        -- 处理音频数据
        log.info("es8311", "received", #data)
    end
end

-- 停止录制
es8311.stop()
```

### 4. 同时播放和录制

```lua
-- 设置为同时模式
es8311.set_mode(es8311.MODE_BOTH)

-- 开始
es8311.start(es8311.MODE_BOTH)

-- 可以同时读写
es8311.write(playback_data, #playback_data)
local recorded_data = es8311.read(1024)
```

## 与ft6336共享I2C总线

ES8311和ft6336可以共享同一I2C总线。关键要点：

1. **使用相同的I2C引脚**：确保两个设备使用相同的SDA和SCL引脚
2. **使用相同的I2C端口号**：两个设备必须使用相同的I2C端口号
3. **使用相同的I2C频率**：建议使用100kHz以确保稳定性
4. **不同的I2C地址**：ES8311地址为0x18，ft6336地址为0x38，不会冲突

### 示例：ES8311和ft6336同时使用

```lua
local es8311 = require "es8311"
local ft6336 = require "ft6336"

-- 初始化ES8311
local es8311_ok = es8311.init({
    sda = 10,
    scl = 9,
    i2c_id = 0,
    fre = 100000
})

-- 初始化ft6336
local ft6336_ok = ft6336.init({
    sda = 10,        -- 相同的I2C引脚
    scl = 9,         -- 相同的I2C引脚
    i2c_id = 0,      -- 相同的I2C端口号
    fre = 100000     -- 相同的I2C频率
})

if es8311_ok and ft6336_ok then
    log.info("main", "both devices initialized")

    -- 可以同时使用
    es8311.start(es8311.MODE_DAC)
    local x, y, state = ft6336.read()

    es8311.write(audio_data, #audio_data)
end
```

## API 参考

### 初始化和配置

| 函数              | 参数                          | 返回值      | 说明                     |
|-------------------|-------------------------------|-------------|--------------------------|
| `es8311.init()`   | 配置表                         | boolean     | 初始化ES8311            |
| `es8311.deinit()` | 无                            | boolean     | 反初始化ES8311          |

### 音频控制

| 函数                      | 参数          | 返回值  | 说明                         |
|---------------------------|---------------|---------|------------------------------|
| `es8311.set_sample_rate()` | 采样率        | boolean | 设置采样率(8k/16k/32k/44.1k/48k) |
| `es8311.set_mode()`       | 模式          | boolean | 设置音频模式                 |
| `es8311.start()`         | 模式          | boolean | 开始音频播放/录制            |
| `es8311.stop()`          | 无            | boolean | 停止音频播放/录制            |

### 音量控制

| 函数                  | 参数      | 返回值    | 说明           |
|-----------------------|-----------|-----------|----------------|
| `es8311.set_volume()` | 0-100    | boolean   | 设置音量       |
| `es8311.get_volume()` | 无        | number/nil | 获取当前音量   |
| `es8311.set_mute()`  | true/false| boolean   | 设置静音       |
| `es8311.get_mute()`  | 无        | boolean/nil| 获取静音状态   |

### 数据传输

| 函数               | 参数              | 返回值         | 说明             |
|--------------------|-------------------|----------------|------------------|
| `es8311.write()`   | 数据, 长度        | number/nil     | 写入音频数据     |
| `es8311.read()`    | 期望长度          | string/nil     | 读取音频数据     |

### 信息查询

| 函数                | 参数      | 返回值        | 说明           |
|---------------------|-----------|---------------|----------------|
| `es8311.get_info()` | 无        | table/nil     | 获取芯片信息    |

### 常量

| 常量              | 值  | 说明                 |
|--------------------|-----|----------------------|
| `MODE_DAC`         | 0   | DAC播放模式         |
| `MODE_ADC`         | 1   | ADC录制模式         |
| `MODE_BOTH`        | 2   | 同时播放和录制模式   |
| `I2C_FREQ_100K`   | 100000 | I2C频率100kHz    |
| `I2C_FREQ_400K`   | 400000 | I2C频率400kHz    |
| `SAMPLE_RATE_8K`   | 8000  | 采样率8kHz        |
| `SAMPLE_RATE_16K`  | 16000 | 采样率16kHz      |
| `SAMPLE_RATE_32K`  | 32000 | 采样率32kHz      |
| `SAMPLE_RATE_44K`  | 44100 | 采样率44.1kHz    |
| `SAMPLE_RATE_48K`  | 48000 | 采样率48kHz      |

## 技术细节

### I2C总线共享实现

本驱动通过以下方式实现I2C总线共享：

1. **检测现有驱动**：初始化时检测I2C驱动是否已安装
2. **避免重复初始化**：如果驱动已存在，直接使用，不重复安装
3. **统一配置**：确保所有设备使用相同的I2C配置参数

这样可以避免I2C总线冲突，实现多设备无缝共享。

### I2S音频接口

驱动使用ESP32S3的I2S外设实现音频数据传输：

- **I2S端口**：I2S_NUM_0
- **采样格式**：16位立体声(I2S_BITS_PER_SAMPLE_16BIT)
- **DMA缓冲区**：8个缓冲区，每个512字节
- **通信格式**：标准I2S格式(I2S_COMM_FORMAT_I2S)

## 故障排除

### 常见问题

1. **初始化失败**
   - 检查硬件连接是否正确
   - 确认I2C地址是否正确(0x18)
   - 使用I2C扫描工具检查设备是否响应

2. **I2C总线冲突**
   - 确保所有设备使用相同的I2C配置
   - 检查是否有其他设备占用I2C总线
   - 确认I2C频率适合所有设备

3. **音频播放无声音**
   - 检查音量设置
   - 确认没有静音
   - 验证I2S引脚连接
   - 检查MCLK是否正常输出

4. **与ft6336不能同时使用**
   - 确认使用相同的I2C引脚
   - 检查I2C端口号是否一致
   - 验证I2C频率设置

## 许可证

本驱动遵循LuatOS项目的许可证。

## 版本历史

- **v1.0** (2026-04-26)
  - 初始版本
  - 支持I2C总线共享
  - 支持音频播放和录制
  - 支持音量控制和静音
