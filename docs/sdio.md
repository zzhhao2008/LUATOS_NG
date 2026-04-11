1# SDIO 通过 FatFs 挂载 SD 卡使用指南

## 概述

本文档介绍如何在 LuatOS ESP32-C3/S3 平台上使用 SDIO 接口通过 FatFs 文件系统挂载和读写 SD/TF 卡。

LuatOS 提供了完整的 SDIO 驱动层和 FatFs 文件系统支持，用户只需通过简单的 Lua API 即可实现 SD 卡的初始化和文件操作。

## 硬件连接

### ESP32-S3/C3 SDIO 默认引脚

ESP32 系列芯片的 SDMMC 控制器默认使用以下 GPIO 引脚：

| 信号 | ESP32-S3 默认引脚 | ESP32-C3 默认引脚 | 说明 |
|------|------------------|------------------|------|
| CLK  | GPIO 12          | GPIO 2           | 时钟信号 |
| CMD  | GPIO 11          | GPIO 3           | 命令信号 |
| D0   | GPIO 13          | GPIO 4           | 数据线 0 |
| D1   | GPIO 14          | GPIO 5           | 数据线 1（4-bit 模式） |
| D2   | GPIO 15          | -                | 数据线 2（4-bit 模式） |
| D3   | GPIO 16          | -                | 数据线 3（4-bit 模式） |

**注意：**
- 系统默认使用 **4-bit 数据总线** 模式以获得更好的性能
- 可以通过 `luat_sdio_set_gpio_config()` 自定义 GPIO 配置
- CD（卡检测）和 WP（写保护）引脚为可选，不使用可设置为 -1

## 软件架构

```
┌─────────────────────┐
│   Lua 应用层         │  fatfs.mount(), io.open() 等
├─────────────────────┤
│   FatFs 文件系统     │  ff.c, ff.h (Chan's FatFs)
├─────────────────────┤
│   Disk I/O 抽象层    │  diskio_sdio.c
├─────────────────────┤
│   SDIO 驱动层        │  luat_sdio_idf5.c
├─────────────────────┤
│   ESP-IDF SDMMC      │  sdmmc_cmd.h, sdmmc_host.h
├─────────────────────┤
│   硬件 SDMMC 控制器  │  ESP32 SDMMC Peripheral
└─────────────────────┘
```

## API 参考

### 1. fatfs.mount() - 挂载 SD 卡

**函数原型：**
```lua
result, err_code = fatfs.mount(mode, mount_point, sdio_id)
```

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| mode | int | 是 | 文件系统模式，使用 `fatfs.SDIO` |
| mount_point | string | 否 | 虚拟文件系统挂载点，默认为 `/fatfs` |
| sdio_id | int | 否 | SDIO 端口 ID（0 或 1），默认为 0 |

**返回值：**

| 返回值 | 类型 | 说明 |
|--------|------|------|
| result | bool | 成功返回 `true`，失败返回 `false` |
| err_code | int | FatFs 错误码，成功时为 `FR_OK (0)` |

**FatFs 错误码参考：**
- `FR_OK (0)`: 成功
- `FR_DISK_ERR (1)`: 底层磁盘 I/O 错误
- `FR_INT_ERR (2)`: 断言失败
- `FR_NOT_READY (3)`: 驱动器未就绪
- `FR_NO_FILE (4)`: 找不到文件
- `FR_NO_PATH (5)`: 找不到路径
- `FR_INVALID_NAME (6)`: 路径名格式无效
- `FR_DENIED (7)`: 由于禁止访问而拒绝访问
- `FR_EXIST (8)`: 禁止访问，因为对象已存在
- `FR_INVALID_OBJECT (9)`: 对象无效
- `FR_WRITE_PROTECTED (10)`: 物理驱动器受写保护
- `FR_INVALID_DRIVE (11)`: 逻辑驱动器号无效
- `FR_NOT_ENABLED (12)`: 卷上没有工作区
- `FR_NO_FILESYSTEM (13)`: 没有有效的 FAT 卷
- `FR_MKFS_ABORTED (14)`: f_mkfs() 因任何原因中止
- `FR_TIMEOUT (15)`: 无法在定义的时间内获得信号量
- `FR_LOCKED (16)`: 根据共享锁定策略，操作被拒绝
- `FR_NOT_ENOUGH_CORE (17)`: 无法分配长文件名工作缓冲区
- `FR_TOO_MANY_OPEN_FILES (18)`: 打开的文件对象数量超过 FF_FS_LOCK
- `FR_INVALID_PARAMETER (19)`: 给定参数无效

**示例：**
```lua
-- 基本用法：使用默认 SDIO ID 0
local result, err = fatfs.mount(fatfs.SDIO, "/sd")
if result then
    log.info("SDIO", "SD card mounted successfully")
else
    log.error("SDIO", "Mount failed, error code:", err)
end

-- 指定 SDIO ID
local result, err = fatfs.mount(fatfs.SDIO, "/sd", 0)
```

### 2. fatfs.getfree() - 获取可用空间

**函数原型：**
```lua
info, err_code = fatfs.getfree(mount_point)
```

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| mount_point | string | 是 | 挂载点，需与 mount() 传入的值一致 |

**返回值：**

| 返回值 | 类型 | 说明 |
|--------|------|------|
| info | table/nil | 成功时返回包含空间信息的 table，失败返回 nil |
| err_code | int | 失败时的 FatFs 错误码 |

**info table 结构：**

| 字段 | 类型 | 说明 |
|------|------|------|
| total_sectors | int | 总扇区数量 |
| free_sectors | int | 空闲扇区数量 |
| total_kb | int | 总容量（KB） |
| free_kb | int | 空闲容量（KB） |

**注意：** 当前扇区大小固定为 512 字节

**示例：**
```lua
local info, err = fatfs.getfree("/sd")
if info then
    log.info("SDIO", "Total space:", info.total_kb, "KB")
    log.info("SDIO", "Free space:", info.free_kb, "KB")
    log.info("SDIO", "Usage:", 
        string.format("%.2f%%", 
        (1 - info.free_kb / info.total_kb) * 100))
else
    log.error("SDIO", "Get free space failed, error:", err)
end
```

### 3. fatfs.debug() - 启用调试模式

**函数原型：**
```lua
fatfs.debug(enable)
```

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| enable | bool/int | 是 | 是否启用调试日志，1 或 true 启用 |

**示例：**
```lua
-- 启用详细调试日志
fatfs.debug(1)

-- 挂载前启用调试，可以查看详细的初始化过程
fatfs.debug(1)
local result, err = fatfs.mount(fatfs.SDIO, "/sd")
```

## 文件操作

挂载成功后，可以使用标准 Lua `io` 库进行文件操作：

### 写入文件

```lua
-- 写入文本文件
local file = io.open("/sd/test.txt", "w")
if file then
    file:write("Hello, SD Card!\n")
    file:write("Line 2\n")
    file:close()
    log.info("SDIO", "File written successfully")
else
    log.error("SDIO", "Failed to open file for writing")
end

-- 写入二进制数据
local file = io.open("/sd/data.bin", "wb")
if file then
    local data = string.char(0x00, 0x01, 0x02, 0xFF)
    file:write(data)
    file:close()
end
```

### 读取文件

```lua
-- 读取整个文件
local file = io.open("/sd/test.txt", "r")
if file then
    local content = file:read("*a")  -- 读取全部内容
    log.info("SDIO", "Content:", content)
    file:close()
end

-- 逐行读取
local file = io.open("/sd/test.txt", "r")
if file then
    for line in file:lines() do
        log.info("SDIO", "Line:", line)
    end
    file:close()
end

-- 读取二进制数据
local file = io.open("/sd/data.bin", "rb")
if file then
    local data = file:read(4)  -- 读取 4 字节
    if data then
        for i = 1, #data do
            log.info("SDIO", string.format("Byte %d: 0x%02X", i, data:byte(i)))
        end
    end
    file:close()
end
```

### 目录操作

```lua
-- 创建目录
os.execute("mkdir /sd/myfolder")

-- 列出目录内容
local files = {}
for file in lfs.dir("/sd") do
    table.insert(files, file)
    log.info("SDIO", "File:", file)
end

-- 检查文件是否存在
local file = io.open("/sd/test.txt", "r")
if file then
    log.info("SDIO", "File exists")
    file:close()
else
    log.info("SDIO", "File does not exist")
end

-- 删除文件
os.remove("/sd/test.txt")

-- 重命名文件
os.rename("/sd/old.txt", "/sd/new.txt")
```

## 完整示例

### 示例 1：基本 SD 卡读写

```lua
-- 引入必要的模块
log.info("SDIO", "Starting SD card test...")

-- 启用调试模式（可选）
fatfs.debug(1)

-- 挂载 SD 卡
local result, err = fatfs.mount(fatfs.SDIO, "/sd")
if not result then
    log.error("SDIO", "Mount failed with error code:", err)
    return
end

log.info("SDIO", "SD card mounted successfully")

-- 获取可用空间
local info, err = fatfs.getfree("/sd")
if info then
    log.info("SDIO", string.format("Total: %d KB, Free: %d KB", 
        info.total_kb, info.free_kb))
end

-- 写入测试文件
local test_file = "/sd/test.txt"
local file = io.open(test_file, "w")
if file then
    file:write("LuatOS SDIO Test\n")
    file:write("Timestamp: " .. os.date("%Y-%m-%d %H:%M:%S") .. "\n")
    file:close()
    log.info("SDIO", "Test file written successfully")
else
    log.error("SDIO", "Failed to write test file")
end

-- 读取测试文件
file = io.open(test_file, "r")
if file then
    local content = file:read("*a")
    log.info("SDIO", "File content:\n" .. content)
    file:close()
end

-- 清理（可选）
-- os.remove(test_file)

log.info("SDIO", "Test completed")
```

### 示例 2：数据记录器

```lua
-- SD 卡数据记录器示例
local MOUNT_POINT = "/sd"
local LOG_FILE = "/sd/sensor_log.csv"
local RECORD_INTERVAL = 5000  -- 5 秒记录一次

-- 初始化 SD 卡
local function init_sd()
    fatfs.debug(0)  -- 生产环境关闭调试
    
    local result, err = fatfs.mount(fatfs.SDIO, MOUNT_POINT)
    if not result then
        log.error("SDIO", "Init failed:", err)
        return false
    end
    
    log.info("SDIO", "SD card ready")
    
    -- 检查是否需要创建新文件（基于日期）
    local today = os.date("%Y%m%d")
    LOG_FILE = string.format("/sd/log_%s.csv", today)
    
    -- 如果文件不存在，写入 CSV 头
    local file = io.open(LOG_FILE, "r")
    if not file then
        file = io.open(LOG_FILE, "w")
        if file then
            file:write("timestamp,value1,value2,value3\n")
            file:close()
            log.info("SDIO", "Created new log file:", LOG_FILE)
        end
    else
        file:close()
    end
    
    return true
end

-- 记录数据
local function log_data(value1, value2, value3)
    local file = io.open(LOG_FILE, "a")
    if not file then
        log.error("SDIO", "Failed to open log file")
        return false
    end
    
    local timestamp = os.date("%Y-%m-%d %H:%M:%S")
    local line = string.format("%s,%.2f,%.2f,%.2f\n", 
        timestamp, value1, value2, value3)
    
    file:write(line)
    file:close()
    
    return true
end

-- 主程序
sys.init()

if not init_sd() then
    log.error("SDIO", "Cannot start without SD card")
    return
end

-- 模拟传感器数据记录
sys.taskInit(function()
    while true do
        -- 这里替换为实际的传感器读取
        local v1 = math.random(0, 100) / 10.0
        local v2 = math.random(0, 100) / 10.0
        local v3 = math.random(0, 100) / 10.0
        
        if log_data(v1, v2, v3) then
            log.info("SDIO", "Data logged")
        end
        
        sys.wait(RECORD_INTERVAL)
    end
end)

sys.run()
```

### 示例 3：文件浏览器

```lua
-- 简单的 SD 卡文件浏览器
local function list_directory(path, indent)
    indent = indent or ""
    
    -- 使用 lfs 库遍历目录
    if lfs then
        for entry in lfs.dir(path) do
            if entry ~= "." and entry ~= ".." then
                local full_path = path .. "/" .. entry
                local attr = lfs.attributes(full_path)
                
                if attr then
                    if attr.mode == "directory" then
                        log.info("FILE", indent .. "[DIR]  " .. entry)
                        list_directory(full_path, indent .. "  ")
                    else
                        local size_str
                        if attr.size < 1024 then
                            size_str = string.format("%d B", attr.size)
                        elseif attr.size < 1024 * 1024 then
                            size_str = string.format("%.1f KB", attr.size / 1024)
                        else
                            size_str = string.format("%.1f MB", attr.size / (1024 * 1024))
                        end
                        log.info("FILE", indent .. "[FILE] " .. entry .. " (" .. size_str .. ")")
                    end
                end
            end
        end
    else
        log.warn("SDIO", "lfs module not available")
    end
end

-- 挂载并浏览
local result = fatfs.mount(fatfs.SDIO, "/sd")
if result then
    log.info("SDIO", "=== SD Card Contents ===")
    list_directory("/sd")
    log.info("SDIO", "========================")
end
```

## 高级用法

### 自定义 GPIO 配置

如果需要自定义 SDIO 引脚，可以在 C 层调用 `luat_sdio_set_gpio_config()`：

```c
#include "luat_sdio.h"

// 配置自定义 GPIO
luat_sdio_gpio_config_t gpio_config = {
    .clk_gpio = 12,   // CLK
    .cmd_gpio = 11,   // CMD
    .d0_gpio = 13,    // D0
    .d1_gpio = 14,    // D1
    .d2_gpio = 15,    // D2
    .d3_gpio = 16,    // D3
    .cd_gpio = -1,    // 不使用卡检测
    .wp_gpio = -1     // 不使用写保护
};

// 在初始化前设置
luat_sdio_set_gpio_config(0, &gpio_config);

// 然后正常初始化
int ret = luat_sdio_init(0);
```

**注意：** 必须在 `fatfs.mount()` 之前设置 GPIO 配置。

### 多 SDIO 端口

系统支持最多 2 个 SDIO 端口（ID 0 和 1）：

```lua
-- 挂载第一个 SD 卡
local result1 = fatfs.mount(fatfs.SDIO, "/sd1", 0)

-- 挂载第二个 SD 卡（如果硬件支持）
local result2 = fatfs.mount(fatfs.SDIO, "/sd2", 1)
```

## 常见问题

### 1. 挂载失败，错误码 FR_NO_FILESYSTEM (13)

**原因：** SD 卡未格式化或文件系统损坏

**解决方法：**
```lua
-- 启用自动格式化（mount 的第 8 个参数）
local result = fatfs.mount(fatfs.SDIO, "/sd", 0, nil, nil, nil, nil, true)
```

或者手动格式化（谨慎使用，会清除所有数据）：
```lua
-- 需要先在 C 层实现格式化功能
```

### 2. 挂载失败，错误码 FR_DISK_ERR (1)

**可能原因：**
- SD 卡未正确插入
- GPIO 连接错误
- SD 卡损坏
- 电源不足

**排查步骤：**
1. 启用调试模式查看详细错误
2. 检查硬件连接
3. 尝试更换 SD 卡
4. 确认 SD 卡电压（3.3V）

### 3. 读写速度慢

**优化建议：**
- 使用高速 SD 卡（Class 10 或以上）
- 确保使用 4-bit 模式（默认已启用）
- 减少频繁的小文件读写，使用缓冲
- 避免在中断中执行文件操作

### 4. 内存不足

**注意事项：**
- FatFs 工作需要一定的 RAM（约几 KB）
- 大文件读写时使用适当的缓冲区大小
- 及时关闭文件句柄释放资源

### 5. 文件乱码

**原因：** 编码问题

**解决方法：**
- 确保使用 UTF-8 编码
- 文件名避免使用特殊字符
- 使用英文文件名和路径

## 性能指标

典型性能（ESP32-S3，4-bit 模式，Class 10 SD 卡）：

| 操作 | 速度 |
|------|------|
| 顺序读取 | ~5-10 MB/s |
| 顺序写入 | ~3-8 MB/s |
| 随机读取 | ~1-3 MB/s |
| 随机写入 | ~0.5-2 MB/s |

**注意：** 实际性能取决于 SD 卡质量、文件系统碎片程度等因素。

## 限制与注意事项

1. **最大文件大小：** 受限于 FAT32，单个文件最大 4GB
2. **文件名长度：** 支持长文件名（LFN），最多 255 字符
3. **并发访问：** 不建议多线程/任务同时访问同一文件
4. **热插拔：** 不支持运行时插拔，需在挂载前插入 SD 卡
5. **电源管理：** SD 卡工作时功耗较大，注意供电稳定性
6. **兼容性：** 支持 SD、SDHC、SDXC 卡，推荐 FAT32/exFAT 格式

## 相关资源

- [FatFs 官方文档](http://elm-chan.org/fsw/ff/00index_e.html)
- [ESP-IDF SDMMC 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/sdmmc.html)
- [LuatOS 官方文档](https://wiki.luatos.com/)

## 更新日志

- **2026-04-11:** 初始版本，基于 ESP-IDF 5.1 和 LuatOS NG
