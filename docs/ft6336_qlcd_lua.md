# LuatOS中ft6336和qlcd组件的Lua使用文档

本文档介绍了LuatOS中ft6336触摸屏驱动和qlcd显示屏驱动的Lua API使用方法。

## ft6336u触摸屏驱动

FT6336U是一款电容式触摸屏控制器，此模块提供了对它的完整支持，并可以与LVGL图形库集成。

### 模块初始化

```lua
local ft6336u = require "ft6336u"
```

### API列表

#### ft6336u.init(config)

初始化ft6336u触摸屏。

参数：
- config (table)：配置参数表
  - config.sda (int)：SDA引脚
  - config.scl (int)：SCL引脚
  - config.rst (int)：RST引脚(可选)
  - config.int (int)：INT引脚(可选)
  - config.fre (int)：I2C频率, 默认400000
  - config.x_limit (int)：X轴最大值
  - config.y_limit (int)：Y轴最大值
  - config.swapxy (bool)：是否交换XY坐标
  - config.mirror_x (bool)：是否镜像X坐标
  - config.mirror_y (bool)：是否镜像Y坐标

返回值：
- (boolean)：是否初始化成功

示例：
```lua
local success = ft6336u.init({
    sda = 11,
    scl = 9,
    rst = 10,    -- 可选
    int = 13,    -- 可选
    fre = 400000,
    x_limit = 480,
    y_limit = 320,
    swapxy = false,
    mirror_x = false,
    mirror_y = false
})
if not success then
    log.error("ft6336u", "init failed")
    return
end
```

#### ft6336u.read()

读取当前触摸状态。

返回值：
- (int)：X坐标
- (int)：Y坐标
- (int)：触摸状态(0=无触摸, 1=触摸)

示例：
```lua
local x, y, state = ft6336u.read()
if state == 1 then
    log.info("touch at", x, y)
end
```

#### ft6336u.lvgl_register()

将触摸屏注册到LVGL，使LVGL能够接收触摸输入。

返回值：
- (boolean)：是否注册成功

示例：
```lua
ft6336u.lvgl_register()
```

#### ft6336u.info()

获取触摸屏信息。

返回值：
- (table)：包含芯片ID、固件版本等信息

示例：
```lua
local info = ft6336u.info()
log.info("touch info", info.chip_id, info.firmware_version)
```

### 常量

- `I2C_FREQ_100K`：100000 (100kHz)
- `I2C_FREQ_400K`：400000 (400kHz)
- `I2C_FREQ_1M`：1000000 (1MHz)

## qlcd显示屏驱动

QLCD是基于ESP LCD驱动的液晶屏驱动，支持ST7789等SPI接口显示屏，并可与LVGL图形库集成。

### 模块初始化

```lua
local qlcd = require "qlcd"
```

### API列表

#### qlcd.init(config)

初始化QLCD液晶屏。

参数：
- config (table)：配置参数表
  - config.width (int)：屏幕宽度，默认320
  - config.height (int)：屏幕高度，默认240
  - config.spi_host (int)：SPI主机号，默认SPI3_HOST(3)
  - config.mosi_pin (int)：MOSI引脚，默认17
  - config.clk_pin (int)：CLK引脚，默认18
  - config.dc_pin (int)：DC引脚，默认15
  - config.rst_pin (int)：RST引脚，默认7
  - config.cs_pin (int)：CS引脚，默认6
  - config.bl_pin (int)：背光引脚，默认16
  - config.freq (int)：SPI频率，默认40MHz
  - config.draw_buf_height (int)：绘制缓冲区高度，默认20

返回值：
- (boolean)：是否初始化成功

示例：
```lua
local success = qlcd.init({
    width = 320,
    height = 240,
    spi_host = 3, -- SPI3_HOST
    mosi_pin = 17,
    clk_pin = 18,
    dc_pin = 15,
    rst_pin = 7,
    cs_pin = 6,
    bl_pin = 16, -- 背光控制引脚
    freq = 40000000, -- 40MHz
    draw_buf_height = 20 -- 绘制缓冲区高度
})
if not success then
    log.error("qlcd", "init failed")
    return
end
```

#### qlcd.lvgl_register()

将LCD注册到LVGL，使LVGL能够渲染到显示屏。

返回值：
- (boolean)：是否注册成功

示例：
```lua
qlcd.lvgl_register()
```

#### qlcd.clear(color)

清屏操作。

参数：
- color (int)：清屏颜色，默认为黑色(0x0000)

返回值：
- (boolean)：是否清屏成功

示例：
```lua
qlcd.clear(0xFFFF) -- 白色清屏
```

### 常量

- `SPI_HOST_1`：SPI1_HOST
- `SPI_HOST_2`：SPI2_HOST
- `SPI_HOST_3`：SPI3_HOST

## 组合使用示例

以下是如何组合使用触摸屏和显示屏的完整示例：

```lua
-- 初始化LCD
local qlcd = require "qlcd"
local success = qlcd.init({
    width = 320,
    height = 240,
    spi_host = 3,
    mosi_pin = 17,
    clk_pin = 18,
    dc_pin = 15,
    rst_pin = 7,
    cs_pin = 6,
    bl_pin = 16,
    freq = 40000000,
    draw_buf_height = 20
})
if not success then
    log.error("qlcd", "init failed")
    return
end

-- 将LCD注册到LVGL
qlcd.lvgl_register()

-- 初始化触摸屏
local ft6336u = require "ft6336u"
success = ft6336u.init({
    sda = 11,
    scl = 9,
    x_limit = 320,
    y_limit = 240
})
if not success then
    log.error("ft6336u", "init failed")
    return
end

-- 将触摸屏注册到LVGL
ft6336u.lvgl_register()

-- 现在可以使用LVGL进行UI开发了
```