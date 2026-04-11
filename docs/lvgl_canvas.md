# LuatOS LVGL Canvas 组件使用指南

## 1. 简介

Canvas（画布）是 LVGL 中一个强大的绘图组件，它允许用户在内存缓冲区中直接绘制像素、图形、文本和图像。在 LuatOS 中，Canvas 常被用于：
- 动态生成图形界面元素
- 实时数据可视化（如波形图、仪表盘）
- **视频帧渲染**（通过直接操作像素缓冲区）
- 自定义控件开发

本项目基于 **LVGL v7.11**，Canvas 模块已完整绑定到 Lua 层。

---

## 2. 核心 API 参考

### 2.1 创建与初始化

#### `lvgl.canvas_create(parent)`
创建一个 Canvas 对象。
- **参数**:
  - `parent`: 父对象（通常为 `lvgl.scr_act()` 获取的当前屏幕）
- **返回**: Canvas 对象的指针（userdata）

```lua
local canvas = lvgl.canvas_create(lvgl.scr_act())
```

#### `lvgl.canvas_set_buffer(canvas, buf, w, h, cf)`
为 Canvas 设置绘图缓冲区。**这是最关键的一步**。
- **参数**:
  - `canvas`: Canvas 对象指针
  - `buf`: 缓冲区指针（通常通过 `zbuff` 模块分配）
  - `w`: 画布宽度（像素）
  - `h`: 画布高度（像素）
  - `cf`: 色彩格式常量（见下文 2.2 节）
- **注意**: LuatOS 扩展实现了此函数，它会在 C 层自动管理内存生命周期。

```lua
local buff = zbuff.create(320 * 240 * 2) -- 分配 RGB565 缓冲区
lvgl.canvas_set_buffer(canvas, buff:ptr(), 320, 240, lvgl.IMG_CF_TRUE_COLOR)
```

### 2.2 色彩格式常量 (Color Format)

Canvas 支持多种色彩格式，常用常量如下：

| 常量 | 说明 | 每像素字节数 |
| :--- | :--- | :--- |
| `lvgl.IMG_CF_TRUE_COLOR` | 真彩色，与系统 `LV_COLOR_DEPTH` 匹配 | 2 (RGB565) 或 4 (ARGB8888) |
| `lvgl.IMG_CF_TRUE_COLOR_ALPHA` | 带 Alpha 通道的真彩色 | 3 或 4 |
| `lvgl.IMG_CF_INDEXED_1BIT` | 1位索引色（2色） | 0.125 |
| `lvgl.IMG_CF_INDEXED_8BIT` | 8位索引色（256色） | 1 |

**提示**: 如果你的 LCD 驱动使用 RGB565，且 `lv_conf.h` 中 `LV_COLOR_DEPTH` 设为 16，则使用 `TRUE_COLOR` 时每个像素占 2 字节。

### 2.3 绘图操作

#### 填充背景
`lvgl.canvas_fill_bg(canvas, color, opa)`
- `color`: 颜色值（如 `lvgl.color_make(255, 0, 0)` 表示红色）
- `opa`: 透明度（`lvgl.OPA_COVER` 表示完全不透明）

#### 拷贝外部缓冲区到画布
`lvgl.canvas_copy_buf(canvas, src, x, y, w, h)`
- 将外部内存 `src` 中的数据拷贝到画布的指定区域。
- **用途**: 用于视频帧更新，直接将解码后的 RGB 数据推送到画布。

#### 绘制基本图形
- `lvgl.canvas_draw_rect(canvas, x, y, w, h, rect_dsc)`: 绘制矩形
- `lvgl.canvas_draw_text(canvas, x, y, max_w, label_dsc, txt, align)`: 绘制文本
- `lvgl.canvas_draw_img(canvas, x, y, src, img_dsc)`: 绘制图片
- `lvgl.canvas_draw_arc(canvas, x, y, r, start_angle, end_angle, arc_dsc)`: 绘制圆弧

### 2.4 像素级操作

- `lvgl.canvas_set_px(canvas, x, y, color)`: 设置单个像素颜色
- `lvgl.canvas_get_px(canvas, x, y)`: 获取单个像素颜色

---

## 3. 实战：实现 RGB 视频播放

你提供的代码使用了 `lcd.draw`，现在我们将逻辑迁移到 LVGL Canvas 上。

### 3.1 前置准备
确保你的 `.rgb` 文件是原始的 RGB565 数据流（无文件头，每像素 2 字节，小端序）。

### 3.2 实现代码

```lua
local sys = require("sys")
local zbuff = require("zbuff")
local fs = require("fs") -- 假设你有 fs 模块获取文件大小
local lvgl = require("lvgl")

local function play_video_on_canvas()
    local video_w = 320
    local video_h = 240
    local rgb_file = "video.rgb"
    
    -- 1. 计算缓冲区大小 (RGB565 每像素 2 字节)
    local buff_size = video_w * video_h * 2
    local file_path = "/sd/" .. rgb_file
    
    -- 获取文件大小用于判断结束
    local file_size = fs.fsize(file_path)
    print("File size:", file_size)

    -- 2. 创建 Canvas 对象
    local scr = lvgl.scr_act()
    local canvas = lvgl.canvas_create(scr)
    
    -- 3. 分配底层缓冲区并绑定到 Canvas
    -- 注意：luat_lv_canvas_set_buffer 扩展函数会自动在 C 层 malloc 内存
    -- 如果你希望手动管理内存，可以使用 zbuff 并通过 copy_buf 更新
    local buff = zbuff.create(buff_size)
    
    -- 方式 A: 使用扩展函数 set_buffer (推荐，性能更好)
    -- 这里的 cf 取决于你的 lv_conf.h 配置。如果是 RGB565 系统，用 TRUE_COLOR
    lvgl.canvas_set_buffer(canvas, buff:ptr(), video_w, video_h, lvgl.IMG_CF_TRUE_COLOR)
    
    -- 设置 Canvas 位置
    lvgl.obj_align(canvas, scr, lvgl.ALIGN_CENTER, 0, 0)

    -- 4. 打开文件并循环读取
    local file = io.open(file_path, "rb")
    if not file then
        print("Failed to open file")
        return
    end

    local file_cnt = 0
    while true do
        -- 填充缓冲区
        local read_len = file:fill(buff)
        if not read_len or read_len == 0 then
            break
        end
        
        file_cnt = file_cnt + read_len
        
        -- 方式 B: 如果使用 zbuff 且未调用 set_buffer，则需要手动拷贝
        -- lvgl.canvas_copy_buf(canvas, buff:ptr(), 0, 0, video_w, video_h)
        
        -- 5. 触发 LVGL 渲染
        -- LVGL 是定时刷新的，调用 task_handler 可以立即处理绘制请求
        lvgl.task_handler()
        
        -- 控制帧率，20ms 约为 50FPS
        sys.wait(20)
        
        if file_size - file_cnt < buff_size then
            break
        end
    end

    -- 处理最后一帧可能不足一帧的情况
    if file_cnt < file_size then
        local remaining = file_size - file_cnt
        file:fill(buff, 0, remaining)
        lvgl.task_handler()
        sys.wait(30)
    end

    file:close()
    print("Video play finished")
end

-- 启动任务
sys.taskInit(play_video_on_canvas)
```

### 3.3 关键点解析

1. **内存管理**: 
   - 示例中使用了 `zbuff.create` 分配内存。在 LuatOS 中，`zbuff` 分配的内存通常是 DMA-capable 的，适合用于图形传输。
   - `lvgl.canvas_set_buffer` 的 LuatOS 扩展实现会自动接管这块内存的生命周期，当 Canvas 被销毁时自动释放。

2. **刷新机制**:
   - `lcd.draw` 是立即硬件传输，而 LVGL 是缓冲渲染。
   - 必须调用 `lvgl.task_handler()` 来让 LVGL 内部将 Canvas 的缓冲区内容混合到屏幕帧缓冲中。

3. **色彩格式对齐**:
   - 如果你的 `lv_conf.h` 中 `LV_COLOR_DEPTH` 是 32，但视频是 RGB565 (16-bit)，直接显示会花屏。
   - **解决方案**: 确保 `LV_COLOR_DEPTH` 设置为 16，或者在 Lua 层进行 RGB565 到 ARGB8888 的转换（性能损耗大，不推荐）。

---

## 4. 常见问题 (FAQ)

**Q: 为什么画面是黑屏或花屏？**
A: 检查 `lvgl.canvas_set_buffer` 中的色彩格式 `cf` 是否与你的系统配置 (`LV_COLOR_DEPTH`) 以及视频文件的实际格式一致。

**Q: 播放速度过快或过慢怎么办？**
A: 调整 `sys.wait(ms)` 的时间。`sys.wait(20)` 对应约 50 帧/秒。如果视频本身帧率较低，请增大等待时间。

**Q: 如何停止播放并释放资源？**
A: 调用 `lvgl.obj_del(canvas)` 即可删除 Canvas 并释放其关联的缓冲区。

**Q: 可以在 Canvas 上叠加文字吗？**
A: 可以。在每一帧更新后，你可以调用 `lvgl.canvas_draw_text` 在画布上绘制水印或状态信息，然后再调用 `lvgl.task_handler()`。

---

## 5. 相关资源
- [LVGL 官方 Canvas 文档](https://docs.lvgl.io/7.11/widgets/obj/canvas.html)
- LuatOS `zbuff` 模块文档
- `LuatOS/components/lvgl/binding/luat_lib_lvgl_canvas_ex.c` (C 层实现源码)
