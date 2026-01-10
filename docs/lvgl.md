# LuatOS中的LVGL图形库使用指南

本文档介绍了LuatOS中可用的LVGL图形库函数和常量。

## 模块简介

LVGL是一个轻量级且功能丰富的开源图形库，专为嵌入式系统设计。在LuatOS中，我们提供了对LVGL的完整封装，使开发者能够使用Lua语言快速开发图形界面应用。

## 初始化

在使用LVGL之前，必须先进行初始化：

```lua
lvgl.init()
```

## 显示屏操作

### 获取屏幕尺寸
- `lvgl.disp_get_hor_res()` - 获取显示屏水平分辨率
- `lvgl.disp_get_ver_res()` - 获取显示屏垂直分辨率
- `lvgl.disp_get_dpi()` - 获取显示屏DPI
- `lvgl.disp_get_size_category()` - 获取显示屏大小类别

### 屏幕旋转
- `lvgl.disp_set_rotation(rotation)` - 设置屏幕旋转方向
- `lvgl.disp_get_rotation()` - 获取当前屏幕旋转方向

## 屏幕操作

### 获取当前屏幕
- `lvgl.scr_act()` - 获取当前活动的screen对象

### 创建和加载屏幕
- `lvgl.scr_load(scr)` - 载入指定的screen
- `lvgl.scr_load_anim(scr, anim_type, time, delay, auto_del)` - 载入指定的screen并使用指定的转场动画

### 系统层操作
- `lvgl.layer_top()` - 获取layer_top
- `lvgl.layer_sys()` - 获取layer_sys

## 主题设置

- `lvgl.theme_set_act(name)` - 设置主题，参数可选值有："default", "mono", "empty", "material_light", "material_dark", "material_no_transition", "material_no_focus"

## 绘图基础对象

### 对象创建与管理
- `lvgl.obj_create(parent, copy)` - 创建基础对象
- `lvgl.obj_del(obj)` - 删除对象
- `lvgl.obj_clean(obj)` - 清空对象的所有子对象
- `lvgl.obj_del_async(obj)` - 异步删除对象

### 对象位置与尺寸
- `lvgl.obj_set_pos(obj, x, y)` - 设置对象位置
- `lvgl.obj_set_x(obj, x)` - 设置对象X坐标
- `lvgl.obj_set_y(obj, y)` - 设置对象Y坐标
- `lvgl.obj_set_size(obj, w, h)` - 设置对象尺寸
- `lvgl.obj_set_width(obj, w)` - 设置对象宽度
- `lvgl.obj_set_height(obj, h)` - 设置对象高度
- `lvgl.obj_align(obj, base, align, x_ofs, y_ofs)` - 对齐对象
- `lvgl.obj_align_mid(obj, base, align, x_ofs, y_ofs)` - 按中心对齐对象

### 对象样式
- `lvgl.obj_add_style(obj, part, style)` - 为对象添加样式
- `lvgl.obj_remove_style(obj, part, style)` - 移除对象样式
- `lvgl.obj_reset_style_list(obj, part)` - 重置对象的样式列表
- `lvgl.obj_refresh_style(obj, part, prop)` - 刷新对象样式
- `lvgl.style_init()` - 初始化样式
- `lvgl.style_set_...` - 设置样式的各种属性

### 对象可见性与状态
- `lvgl.obj_set_hidden(obj, hidden)` - 设置对象隐藏状态
- `lvgl.obj_set_click(obj, en)` - 启用/禁用对象点击
- `lvgl.obj_set_drag(obj, en)` - 启用/禁用对象拖拽
- `lvgl.obj_set_state(obj, state)` - 设置对象状态

## 控件

### 按钮 (Button)
- `lvgl.btn_create(parent)` - 创建按钮
- `lvgl.btn_set_checkable(btn, checkable)` - 设置按钮是否可勾选
- `lvgl.btn_set_state(btn, state)` - 设置按钮状态
- `lvgl.btn_toggle(btn)` - 切换按钮状态

### 标签 (Label)
- `lvgl.label_create(parent)` - 创建标签
- `lvgl.label_set_text(label, txt)` - 设置标签文本
- `lvgl.label_set_long_mode(label, long_mode)` - 设置长文本模式
- `lvgl.label_set_align(label, align)` - 设置文本对齐方式

### 图像 (Image)
- `lvgl.img_create(parent)` - 创建图像对象
- `lvgl.img_set_src(img, src)` - 设置图像源
- `lvgl.img_set_offset_x(img, x)` - 设置X轴偏移
- `lvgl.img_set_offset_y(img, y)` - 设置Y轴偏移
- `lvgl.img_set_angle(img, angle)` - 设置旋转角度
- `lvgl.img_set_zoom(img, zoom)` - 设置缩放比例

### 滑动条 (Slider)
- `lvgl.slider_create(parent)` - 创建滑动条
- `lvgl.slider_set_value(slider, value)` - 设置滑动条值
- `lvgl.slider_set_range(slider, min, max)` - 设置滑动条范围
- `lvgl.slider_set_type(slider, type)` - 设置滑动条类型

### 滚轮选择器 (Roller)
- `lvgl.roller_create(parent)` - 创建滚轮选择器
- `lvgl.roller_set_options(roller, options, mode)` - 设置选项
- `lvgl.roller_set_selected(roller, sel_opt, anim_en)` - 设置选中项

### 开关 (Switch)
- `lvgl.switch_create(parent)` - 创建开关
- `lvgl.switch_on(sw)` - 打开开关
- `lvgl.switch_off(sw)` - 关闭开关
- `lvgl.switch_toggle(sw)` - 切换开关状态

### 文本输入框 (Textarea)
- `lvgl.textarea_create(parent)` - 创建文本输入框
- `lvgl.textarea_set_text(ta, txt)` - 设置文本
- `lvgl.textarea_add_text(ta, txt)` - 添加文本
- `lvgl.textarea_set_cursor_pos(ta, pos)` - 设置光标位置
- `lvgl.textarea_set_placeholder_text(ta, txt)` - 设置占位符文本

### 进度条 (Bar)
- `lvgl.bar_create(parent)` - 创建进度条
- `lvgl.bar_set_value(bar, value, anim_en)` - 设置进度值
- `lvgl.bar_set_range(bar, min, max)` - 设置范围

### 圆弧 (Arc)
- `lvgl.arc_create(parent)` - 创建圆弧
- `lvgl.arc_set_angles(arc, start, end)` - 设置起始和结束角度
- `lvgl.arc_set_range(arc, min, max)` - 设置数值范围
- `lvgl.arc_set_value(arc, value)` - 设置当前值

### 按钮矩阵 (Button matrix)
- `lvgl.btnmatrix_create(parent)` - 创建按钮矩阵
- `lvgl.btnmatrix_set_btn_ctrl(mtx, btn_id, ctrl)` - 设置按钮控制属性
- `lvgl.btnmatrix_set_one_check(mtx, one_chk)` - 设置单选模式

### 表格 (Table)
- `lvgl.table_create(parent)` - 创建表格
- `lvgl.table_set_cell_value(table, row, col, txt)` - 设置单元格值
- `lvgl.table_set_col_width(table, col_id, w)` - 设置列宽

### 日历 (Calendar)
- `lvgl.calendar_create(parent)` - 创建日历
- `lvgl.calendar_set_today_date(calendar, year, month, day)` - 设置今天日期

### 图表 (Chart)
- `lvgl.chart_create(parent)` - 创建图表
- `lvgl.chart_add_series(chart, color)` - 添加数据系列
- `lvgl.chart_set_type(chart, type)` - 设置图表类型
- `lvgl.chart_set_next(chart, ser, value)` - 添加下一个点

### 二维码 (QR Code)
- `lvgl.qrcode_create(parent)` - 创建二维码控件
- `lvgl.qrcode_update(qrcode, data, data_len)` - 更新二维码内容

## 动画

### 创建和管理动画
- `lvgl.anim_init()` - 初始化动画
- `lvgl.anim_set_var(anim, var)` - 设置动画变量
- `lvgl.anim_set_time(anim, time)` - 设置动画持续时间
- `lvgl.anim_set_values(anim, start, end)` - 设置动画起始和结束值
- `lvgl.anim_set_path(anim, path)` - 设置动画路径
- `lvgl.anim_start(anim)` - 启动动画

## 颜色操作

- `lvgl.color_make(r, g, b)` - 创建颜色
- `lvgl.color_hex(hex)` - 从十六进制值创建颜色
- `lvgl.color_lighten(color, ratio)` - 使颜色变亮
- `lvgl.color_darken(color, ratio)` - 使颜色变暗

## 输入设备

### 键盘
- `lvgl.keyboard_create(parent)` - 创建键盘
- `lvgl.keyboard_set_textarea(kb, ta)` - 将键盘关联到文本区域

## 符号常量

LVGL提供了一系列预定义的符号，用于在界面中显示图标：

- `lvgl.SYMBOL_AUDIO` - 音频符号
- `lvgl.SYMBOL_VIDEO` - 视频符号
- `lvgl.SYMBOL_LIST` - 列表符号
- `lvgl.SYMBOL_OK` - 确认符号
- `lvgl.SYMBOL_CLOSE` - 关闭符号
- `lvgl.SYMBOL_POWER` - 电源符号
- `lvgl.SYMBOL_SETTINGS` - 设置符号
- `lvgl.SYMBOL_HOME` - 首页符号
- `lvgl.SYMBOL_DOWNLOAD` - 下载符号
- `lvgl.SYMBOL_DRIVE` - 驱动器符号
- `lvgl.SYMBOL_REFRESH` - 刷新符号
- `lvgl.SYMBOL_MUTE` - 静音符号
- `lvgl.SYMBOL_VOLUME_MID` - 中等音量符号
- `lvgl.SYMBOL_VOLUME_MAX` - 最大音量符号
- `lvgl.SYMBOL_IMAGE` - 图像符号
- `lvgl.SYMBOL_EDIT` - 编辑符号
- `lvgl.SYMBOL_PREV` - 上一个符号
- `lvgl.SYMBOL_PLAY` - 播放符号
- `lvgl.SYMBOL_PAUSE` - 暂停符号
- `lvgl.SYMBOL_STOP` - 停止符号
- `lvgl.SYMBOL_NEXT` - 下一个符号
- `lvgl.SYMBOL_EJECT` - 弹出符号
- `lvgl.SYMBOL_LEFT` - 左箭头符号
- `lvgl.SYMBOL_RIGHT` - 右箭头符号
- `lvgl.SYMBOL_PLUS` - 加号符号
- `lvgl.SYMBOL_MINUS` - 减号符号
- `lvgl.SYMBOL_EYE_OPEN` - 打开眼睛符号
- `lvgl.SYMBOL_EYE_CLOSE` - 关闭眼睛符号
- `lvgl.SYMBOL_WARNING` - 警告符号
- `lvgl.SYMBOL_SHUFFLE` - 随机符号
- `lvgl.SYMBOL_UP` - 上箭头符号
- `lvgl.SYMBOL_DOWN` - 下箭头符号
- `lvgl.SYMBOL_LOOP` - 循环符号
- `lvgl.SYMBOL_DIRECTORY` - 目录符号
- `lvgl.SYMBOL_UPLOAD` - 上传符号
- `lvgl.SYMBOL_CALL` - 通话符号
- `lvgl.SYMBOL_CUT` - 剪切符号
- `lvgl.SYMBOL_COPY` - 复制符号
- `lvgl.SYMBOL_SAVE` - 保存符号
- `lvgl.SYMBOL_CHARGE` - 充电符号
- `lvgl.SYMBOL_PASTE` - 粘贴符号
- `lvgl.SYMBOL_BELL` - 铃铛符号
- `lvgl.SYMBOL_KEYBOARD` - 键盘符号
- `lvgl.SYMBOL_GPS` - GPS符号
- `lvgl.SYMBOL_FILE` - 文件符号
- `lvgl.SYMBOL_WIFI` - WiFi符号
- `lvgl.SYMBOL_BATTERY_FULL` - 满电量符号
- `lvgl.SYMBOL_BATTERY_3` - 电量3/4符号
- `lvgl.SYMBOL_BATTERY_2` - 电量1/2符号
- `lvgl.SYMBOL_BATTERY_1` - 电量1/4符号
- `lvgl.SYMBOL_BATTERY_EMPTY` - 低电量符号
- `lvgl.SYMBOL_USB` - USB符号
- `lvgl.SYMBOL_BLUETOOTH` - 蓝牙符号
- `lvgl.SYMBOL_TRASH` - 回收站符号
- `lvgl.SYMBOL_BACKSPACE` - 退格符号
- `lvgl.SYMBOL_SD_CARD` - SD卡符号
- `lvgl.SYMBOL_NEW_LINE` - 换行符号
- `lvgl.SYMBOL_DUMMY` - 占位符符号
- `lvgl.SYMBOL_BULLET` - 项目符号

## 样式常量

### 对齐方式
- `lvgl.ALIGN_CENTER` - 居中对齐
- `lvgl.ALIGN_IN_TOP_LEFT` - 左上角内部对齐
- `lvgl.ALIGN_IN_TOP_MID` - 顶部中间内部对齐
- `lvgl.ALIGN_IN_TOP_RIGHT` - 右上角内部对齐
- `lvgl.ALIGN_IN_BOTTOM_LEFT` - 左下角内部对齐
- `lvgl.ALIGN_IN_BOTTOM_MID` - 底部中间内部对齐
- `lvgl.ALIGN_IN_BOTTOM_RIGHT` - 右下角内部对齐
- `lvgl.ALIGN_OUT_TOP_LEFT` - 左上角外部对齐
- `lvgl.ALIGN_OUT_TOP_RIGHT` - 右上角外部对齐
- `lvgl.ALIGN_OUT_BOTTOM_LEFT` - 左下角外部对齐
- `lvgl.ALIGN_OUT_BOTTOM_RIGHT` - 右下角外部对齐

### 透明度
- `lvgl.OPA_TRANSP` - 完全透明
- `lvgl.OPA_0` - 0% 不透明 (完全透明)
- `lvgl.OPA_10` - 10% 不透明
- `lvgl.OPA_20` - 20% 不透明
- `lvgl.OPA_30` - 30% 不透明
- `lvgl.OPA_40` - 40% 不透明
- `lvgl.OPA_50` - 50% 不透明
- `lvgl.OPA_60` - 60% 不透明
- `lvgl.OPA_70` - 70% 不透明
- `lvgl.OPA_80` - 80% 不透明
- `lvgl.OPA_90` - 90% 不透明
- `lvgl.OPA_100` - 100% 不透明
- `lvgl.OPA_COVER` - 完全不透明

### 屏幕加载动画类型
- `lvgl.SCR_LOAD_ANIM_NONE` - 无动画
- `lvgl.SCR_LOAD_ANIM_OVER_LEFT` - 从右侧覆盖到左侧
- `lvgl.SCR_LOAD_ANIM_OVER_RIGHT` - 从左侧覆盖到右侧
- `lvgl.SCR_LOAD_ANIM_OVER_TOP` - 从底部覆盖到顶部
- `lvgl.SCR_LOAD_ANIM_OVER_BOTTOM` - 从顶部覆盖到底部
- `lvgl.SCR_LOAD_ANIM_MOVE_LEFT` - 向左移动
- `lvgl.SCR_LOAD_ANIM_MOVE_RIGHT` - 向右移动
- `lvgl.SCR_LOAD_ANIM_MOVE_TOP` - 向上移动
- `lvgl.SCR_LOAD_ANIM_MOVE_BOTTOM` - 向下移动
- `lvgl.SCR_LOAD_ANIM_FADE_ON` - 淡入效果

### 事件类型
- `lvgl.EVENT_PRESSED` - 按下事件
- `lvgl.EVENT_PRESSING` - 正在按下事件
- `lvgl.EVENT_PRESS_LOST` - 失去按压事件
- `lvgl.EVENT_SHORT_CLICKED` - 短点击事件
- `lvgl.EVENT_LONG_PRESSED` - 长按事件
- `lvgl.EVENT_LONG_PRESSED_REPEAT` - 长按重复事件
- `lvgl.EVENT_CLICKED` - 点击事件
- `lvgl.EVENT_RELEASED` - 释放事件
- `lvgl.EVENT_DRAG_BEGIN` - 拖拽开始事件
- `lvgl.EVENT_DRAG_END` - 拖拽结束事件
- `lvgl.EVENT_DRAG_THROW_BEGIN` - 拖拽投掷开始事件
- `lvgl.EVENT_GESTURE` - 手势事件
- `lvgl.EVENT_KEY` - 按键事件
- `lvgl.EVENT_FOCUSED` - 获得焦点事件
- `lvgl.EVENT_DEFOCUSED` - 失去焦点事件
- `lvgl.EVENT_LEAVE` - 离开事件
- `lvgl.EVENT_VALUE_CHANGED` - 数值改变事件
- `lvgl.EVENT_INSERT` - 插入事件
- `lvgl.EVENT_REFRESH` - 刷新事件
- `lvgl.EVENT_APPLY` - 应用事件
- `lvgl.EVENT_CANCEL` - 取消事件
- `lvgl.EVENT_DELETE` - 删除事件

### 状态标志
- `lvgl.STATE_DEFAULT` - 默认状态
- `lvgl.STATE_CHECKED` - 勾选状态
- `lvgl.STATE_FOCUSED` - 焦点状态
- `lvgl.STATE_EDITED` - 编辑状态
- `lvgl.STATE_HOVERED` - 悬停状态
- `lvgl.STATE_PRESSED` - 按下状态
- `lvgl.STATE_DISABLED` - 禁用状态

## 输入设备类型
- `lvgl.INDEV_TYPE_NONE` - 无输入设备
- `lvgl.INDEV_TYPE_POINTER` - 指针设备 (如触摸屏)
- `lvgl.INDEV_TYPE_KEYPAD` - 键盘设备
- `lvgl.INDEV_TYPE_BUTTON` - 按钮设备
- `lvgl.INDEV_TYPE_ENCODER` - 编码器设备

## 输入设备状态
- `lvgl.INDEV_STATE_REL` - 释放状态
- `lvgl.INDEV_STATE_PR` - 按下状态

## 布局常量
- `lvgl.LAYOUT_OFF` - 无布局
- `lvgl.LAYOUT_CENTER` - 居中布局
- `lvgl.LAYOUT_COLUMN_LEFT` - 左对齐列布局
- `lvgl.LAYOUT_COLUMN_MID` - 中间对齐列布局
- `lvgl.LAYOUT_COLUMN_RIGHT` - 右对齐列布局
- `lvgl.LAYOUT_ROW_TOP` - 顶对齐行布局
- `lvgl.LAYOUT_ROW_MID` - 中间对齐行布局
- `lvgl.LAYOUT_ROW_BOTTOM` - 底对齐行布局

## 适合模式 (Fit Modes)
- `lvgl.FIT_NONE` - 不调整大小
- `lvgl.FIT_TIGHT` - 紧凑适应
- `lvgl.FIT_PARENT` - 适应父容器
- `lvgl.FIT_MAX` - 最大化适应

## 滚轮模式
- `lvgl.ROLLER_MODE_NORMAL` - 正常模式
- `lvgl.ROLLER_MODE_INFINITE` - 无限循环模式

## 滑动条类型
- `lvgl.SLIDER_TYPE_NORMAL` - 正常类型
- `lvgl.SLIDER_TYPE_SYMMETRICAL` - 对称类型
- `lvgl.SLIDER_TYPE_RANGE` - 范围类型

## 图表类型
- `lvgl.CHART_TYPE_NONE` - 无类型
- `lvgl.CHART_TYPE_LINE` - 线型图
- `lvgl.CHART_TYPE_COLUMN` - 柱状图

## 按钮矩阵控制标志
- `lvgl.BTNMATRIX_CTRL_HIDDEN` - 隐藏按钮
- `lvgl.BTNMATRIX_CTRL_NO_REPEAT` - 不重复按下
- `lvgl.BTNMATRIX_CTRL_DISABLED` - 禁用按钮
- `lvgl.BTNMATRIX_CTRL_CHECKABLE` - 可勾选按钮
- `lvgl.BTNMATRIX_CTRL_CHECK_STATE` - 勾选状态
- `lvgl.BTNMATRIX_CTRL_CLICK_TRIG` - 点击触发

## 特殊键值
- `lvgl.KEY_UP` - 上箭头键
- `lvgl.KEY_DOWN` - 下箭头键
- `lvgl.KEY_RIGHT` - 右箭头键
- `lvgl.KEY_LEFT` - 左箭头键
- `lvgl.KEY_ESC` - ESC键
- `lvgl.KEY_DEL` - DEL键
- `lvgl.KEY_BACKSPACE` - 退格键
- `lvgl.KEY_ENTER` - 回车键
- `lvgl.KEY_NEXT` - 下一个键
- `lvgl.KEY_PREV` - 上一个键
- `lvgl.KEY_HOME` - Home键
- `lvgl.KEY_END` - End键

## 标签长文本模式
- `lvgl.LABEL_LONG_EXPAND` - 扩展模式
- `lvgl.LABEL_LONG_BREAK` - 换行模式
- `lvgl.LABEL_LONG_DOT` - 省略号模式
- `lvgl.LABEL_LONG_SROLL` - 滚动模式
- `lvgl.LABEL_LONG_SROLL_CIRC` - 循环滚动模式
- `lvgl.LABEL_LONG_CROP` - 截取模式

## 标签对齐方式
- `lvgl.LABEL_ALIGN_LEFT` - 左对齐
- `lvgl.LABEL_ALIGN_CENTER` - 居中对齐
- `lvgl.LABEL_ALIGN_RIGHT` - 右对齐
- `lvgl.LABEL_ALIGN_AUTO` - 自动对齐

## 圆弧类型
- `lvgl.ARC_TYPE_NORMAL` - 正常类型
- `lvgl.ARC_TYPE_SYMMETRIC` - 对称类型
- `lvgl.ARC_TYPE_REVERSE` - 反向类型

## 其他功能

### 字体操作
- `lvgl.font_load(name)` - 加载字体
- `lvgl.font_free(font)` - 释放字体

### 休眠控制
- `lvgl.sleep(enable)` - 控制LVGL休眠，暂停/恢复刷新定时器

## 示例代码

以下是一个简单的LVGL界面示例：

```lua
-- 初始化LVGL
lvgl.init()

-- 创建一个屏幕
local scr = lvgl.obj_create(nil, nil)

-- 创建一个按钮
local btn = lvgl.btn_create(scr)
lvgl.obj_set_size(btn, 100, 50)
lvgl.obj_align(btn, scr, lvgl.ALIGN_CENTER, 0, 0)

-- 创建一个标签
local label = lvgl.label_create(btn)
lvgl.label_set_text(label, "点击我")

-- 加载屏幕
lvgl.scr_load(scr)
```

这个示例创建了一个带有按钮的简单界面，并将其设置为当前显示的屏幕。