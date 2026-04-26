# ES8311 驱动更新日志

## v1.1 - ESP-IDF 5.1.6 适配 (2026-04-26)

### 主要更新

#### I2C API 更新
- ✅ 更新为 ESP-IDF 5.x 的 I2C 主设备 API
- ✅ 使用 `i2c_new_master_bus()` 替代旧的 `i2c_driver_install()`
- ✅ 使用 `i2c_master_transmit()` 和 `i2c_master_receive()` 进行I2C通信
- ✅ 支持I2C总线共享，通过 `ESP_ERR_INVALID_STATE` 检测现有总线
- ✅ 添加I2C总线句柄管理 (`g_i2c_bus_handle`)

#### I2S API 更新
- ✅ 更新为 ESP-IDF 5.x 的新 I2S 标准接口
- ✅ 使用 `driver/i2s_std.h` 替代已弃用的 `driver/i2s.h`
- ✅ 使用 `i2s_new_channel()` 创建TX/RX通道
- ✅ 使用 `i2s_channel_write()` 和 `i2s_channel_read()` 进行数据传输
- ✅ 独立的TX和RX通道句柄 (`g_tx_handle`, `g_rx_handle`)

#### 修复的问题
- ✅ 修复编译错误："implicit declaration of function 'i2c_get_config'"
- ✅ 修复编译错误："too few arguments to function 'i2c_driver_install'"
- ✅ 修复弃用警告："This set of I2S APIs has been deprecated"
- ✅ 修复弃用警告："I2S_COMM_FORMAT_I2S is deprecated"

### API 兼容性

#### 保持兼容的 Lua API
以下 Lua API 保持不变，用户代码无需修改：

```lua
-- 初始化
es8311.init(config)

-- 播放控制
es8311.set_mode(es8311.MODE_DAC)
es8311.set_sample_rate(44100)
es8311.set_volume(80)
es8311.start(es8311.MODE_DAC)
es8311.write(audio_data)
es8311.stop()

-- 录制控制
es8311.set_mode(es8311.MODE_ADC)
es8311.start(es8311.MODE_ADC)
local data = es8311.read(1024)
es8311.stop()

-- 音量控制
es8311.set_volume(50)
local vol = es8311.get_volume()
es8311.set_mute(true)
local muted = es8311.get_mute()

-- 设备信息
local info = es8311.get_info()
```

### 新增功能

- ✅ 更好的I2C总线共享支持
- ✅ 独立的TX/RX通道管理
- ✅ 改进的错误处理和日志记录

### 已知限制

- 需要ESP-IDF 5.1或更高版本
- I2S数据传输需要DMA缓冲区（默认512字节）
- 同时播放和录制需要足够的DMA内存

### 测试建议

1. **基本初始化测试**
```lua
local es8311 = require "es8311"
local ok = es8311.init({
    sda = 10, scl = 9,
    i2c_id = 0, fre = 100000
})
print("Init:", ok)
```

2. **I2C总线共享测试**
```lua
local es8311 = require "es8311"
local ft6336 = require "ft6336"

local ok1 = es8311.init({sda = 10, scl = 9, i2c_id = 0})
local ok2 = ft6336.init({sda = 10, scl = 9, i2c_id = 0})
print("ES8311:", ok1, "FT6336:", ok2)
```

3. **音频播放测试**
```lua
es8311.set_mode(es8311.MODE_DAC)
es8311.set_sample_rate(44100)
es8311.set_volume(50)
es8311.start(es8311.MODE_DAC)
es8311.write(audio_data, #audio_data)
es8311.stop()
```

4. **音频录制测试**
```lua
es8311.set_mode(es8311.MODE_ADC)
es8311.set_sample_rate(16000)
es8311.start(es8311.MODE_ADC)
local data = es8311.read(1024)
print("Recorded:", #data)
es8311.stop()
```

## v1.0 - 初始版本 (2026-04-26)

### 功能特性
- ✅ 基础I2C驱动
- ✅ 基础I2S播放/录制
- ✅ 音量控制和静音
- ✅ 多种采样率支持
- ✅ Lua API绑定

## 从旧版本迁移

如果您之前使用了v1.0版本，升级到v1.1需要：

1. **重新编译项目**：新的API需要重新编译
2. **ESP-IDF版本**：确保使用ESP-IDF 5.1或更高版本
3. **Lua代码**：无需修改，Lua API保持兼容
4. **配置参数**：配置参数格式保持不变

## 技术细节

### I2C 通信流程

```
初始化流程：
1. 尝试创建I2C主设备总线
2. 如果返回ESP_ERR_INVALID_STATE，说明总线已存在（共享）
3. 保存总线句柄（如果是自己创建的）
4. 为每次I2C操作创建临时设备句柄
5. 操作完成后删除设备句柄

通信流程：
1. i2c_new_master_device() - 创建设备句柄
2. i2c_master_transmit() - 写入寄存器地址
3. i2c_master_receive() - 读取数据
4. i2c_del_master_device() - 删除设备句柄
```

### I2S 通信流程

```
初始化流程：
1. 配置I2S标准参数（时钟、插槽、GPIO）
2. 根据模式创建TX/RX通道
3. 启用通道

播放流程：
1. 检查TX句柄是否存在
2. i2s_channel_write() - 写入音频数据
3. 等待写入完成

录制流程：
1. 检查RX句柄是否存在
2. i2s_channel_read() - 读取音频数据
3. 返回实际读取的字节数
```

## 贡献

欢迎提交问题报告和功能请求！

## 许可证

本驱动遵循LuatOS项目的许可证。
