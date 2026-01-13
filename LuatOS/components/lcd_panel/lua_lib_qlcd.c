/*
@module  qlcd
@summary QLCD液晶屏驱动
@version 1.0
@date    2026.01.13
@demo qlcd
@usage
-- 初始化LCD
local qlcd = require "qlcd"
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
-- 将LCD注册到LVGL
qlcd.lvgl_register()
*/
#include "luat_base.h"
#include "luat_gpio.h"
#include "luat_rtos.h"
#include "esp_log.h"
#include "esp_lcd_types.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"  // 添加GPIO_NUM_NC定义
#include "lvgl.h"
#include "luat_lcd.h"

#define LUAT_LOG_TAG "qlcd"
#include "luat_log.h"

// 定义默认参数
#define DEFAULT_LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define DEFAULT_LCD_SPI_NUM (SPI3_HOST)
#define LCD_CMD_BITS (8)
#define LCD_PARAM_BITS (8)
#define DEFAULT_LCD_BITS_PER_PIXEL (16)
#define DEFAULT_LCD_DRAW_BUF_HEIGHT (20)

// 全局变量
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static lv_disp_t *disp = NULL;
static bool is_initialized = false;

// 配置结构
typedef struct {
    int width;
    int height;
    int spi_host;
    int mosi_pin;
    int clk_pin;
    int dc_pin;
    int rst_pin;
    int cs_pin;
    int bl_pin;
    int freq;
    int draw_buf_height;
} qlcd_config_t;

static qlcd_config_t lcd_config = {
    .width = 320,
    .height = 240,
    .spi_host = 3, // SPI3_HOST
    .mosi_pin = 17,
    .clk_pin = 18,
    .dc_pin = 15,
    .rst_pin = 7,
    .cs_pin = 6,
    .bl_pin = 16, // 背光控制引脚
    .freq = 40000000, // 40MHz
    .draw_buf_height = 20
};

// 声明显示刷新回调函数
static void _disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);

/*
初始化QLCD液晶屏
@api qlcd.init(config)
@table config 配置参数
@int config.width 屏幕宽度，默认320
@int config.height 屏幕高度，默认240
@int config.spi_host SPI主机号，默认SPI3_HOST(3)
@int config.mosi_pin MOSI引脚，默认17
@int config.clk_pin CLK引脚，默认18
@int config.dc_pin DC引脚，默认15
@int config.rst_pin RST引脚，默认7
@int config.cs_pin CS引脚，默认6
@int config.bl_pin 背光引脚，默认16
@int config.freq SPI频率，默认40MHz
@int config.draw_buf_height 绘制缓冲区高度，默认20
@return boolean 是否成功
@usage
local success = qlcd.init({
    width = 320,
    height = 240,
    freq = 40000000
})
*/
static int l_qlcd_init(lua_State *L) {
    if (lua_type(L, 1) == LUA_TTABLE) {
        // 从配置表读取参数
        lua_getfield(L, 1, "width");
        if (lua_isinteger(L, -1)) {
            lcd_config.width = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "height");
        if (lua_isinteger(L, -1)) {
            lcd_config.height = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "spi_host");
        if (lua_isinteger(L, -1)) {
            lcd_config.spi_host = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "mosi_pin");
        if (lua_isinteger(L, -1)) {
            lcd_config.mosi_pin = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "clk_pin");
        if (lua_isinteger(L, -1)) {
            lcd_config.clk_pin = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "dc_pin");
        if (lua_isinteger(L, -1)) {
            lcd_config.dc_pin = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "rst_pin");
        if (lua_isinteger(L, -1)) {
            lcd_config.rst_pin = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "cs_pin");
        if (lua_isinteger(L, -1)) {
            lcd_config.cs_pin = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "bl_pin");
        if (lua_isinteger(L, -1)) {
            lcd_config.bl_pin = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "freq");
        if (lua_isinteger(L, -1)) {
            lcd_config.freq = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "draw_buf_height");
        if (lua_isinteger(L, -1)) {
            lcd_config.draw_buf_height = luaL_checkinteger(L, -1);
        }
        lua_pop(L, 1);
    }

    // 初始化SPI总线
    ESP_LOGI(LUAT_LOG_TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = lcd_config.clk_pin,
        .mosi_io_num = lcd_config.mosi_pin,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = lcd_config.width * lcd_config.height * sizeof(uint16_t),
    };
    esp_err_t ret = spi_bus_initialize(lcd_config.spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        LLOGE("SPI init failed: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }

    // 初始化面板IO
    ESP_LOGI(LUAT_LOG_TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = lcd_config.dc_pin,
        .cs_gpio_num = lcd_config.cs_pin,
        .pclk_hz = lcd_config.freq,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 2,
        .trans_queue_depth = 10,
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)lcd_config.spi_host, &io_config, &io_handle);
    if (ret != ESP_OK) {
        LLOGE("New panel IO failed: %s", esp_err_to_name(ret));
        spi_bus_free(lcd_config.spi_host);
        lua_pushboolean(L, 0);
        return 1;
    }

    // 初始化ST7789面板
    ESP_LOGD(LUAT_LOG_TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = lcd_config.rst_pin,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = DEFAULT_LCD_BITS_PER_PIXEL,
    };
    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        LLOGE("New panel failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(io_handle);
        spi_bus_free(lcd_config.spi_host);
        lua_pushboolean(L, 0);
        return 1;
    }

    // 初始化面板
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, true);  // 颜色反转
    esp_lcd_panel_swap_xy(panel_handle, true);       // 显示翻转
    esp_lcd_panel_mirror(panel_handle, false, true); // 镜像

    // 开启显示
    esp_lcd_panel_disp_on_off(panel_handle, true);
    
    // 控制背光
    if (lcd_config.bl_pin >= 0) {
        luat_gpio_mode(lcd_config.bl_pin, Luat_GPIO_OUTPUT, Luat_GPIO_PULLUP, 1);
    }

    is_initialized = true;
    LLOGI("QLCD initialized successfully");
    lua_pushboolean(L, 1);
    return 1;
}

/*
将LCD注册到LVGL
@api qlcd.lvgl_register()
@return boolean 是否成功
@usage
qlcd.lvgl_register()
*/
static int l_qlcd_lvgl_register(lua_State *L) {
    if (!is_initialized) {
        LLOGE("QLCD not initialized");
        lua_pushboolean(L, 0);
        return 1;
    }

    // 初始化LVGL
    lv_init();

    // 创建显示缓冲区
    static lv_color_t *buf = NULL;
    if (buf == NULL) {
        buf = (lv_color_t *)heap_caps_malloc(
            lcd_config.width * lcd_config.draw_buf_height * sizeof(lv_color_t),
            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (buf == NULL) {
            LLOGE("Failed to allocate LVGL buffer");
            lua_pushboolean(L, 0);
            return 1;
        }
    }

    // 关联缓冲区到LVGL - 修复LVGL版本兼容性问题
    lv_disp_buf_t draw_buf;  // 使用lv_disp_buf_t而不是lv_disp_draw_buf_t
    lv_disp_buf_init(&draw_buf, buf, NULL, lcd_config.width * lcd_config.draw_buf_height);  // 使用lv_disp_buf_init

    // 初始化显示驱动
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = lcd_config.width;
    disp_drv.ver_res = lcd_config.height;
    disp_drv.flush_cb = _disp_flush;  // 此处不会再报错，因为函数已在前面声明
    disp_drv.buffer = &draw_buf;  // 使用buffer而不是draw_buf
    disp = lv_disp_drv_register(&disp_drv);

    LLOGI("QLCD registered with LVGL successfully");
    lua_pushboolean(L, 1);
    return 1;
}

/*
显示刷新回调函数
*/
static void _disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    // 发送数据到LCD
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, (uint16_t *)color_map);

    // 通知LVGL完成刷新
    lv_disp_flush_ready(drv);
}

/*
清屏
@api qlcd.clear(color)
@int color 清屏颜色，默认为黑色
@usage
qlcd.clear(0xFFFF) -- 白色清屏
*/
static int l_qlcd_clear(lua_State *L) {
    if (!is_initialized) {
        LLOGE("QLCD not initialized");
        lua_pushboolean(L, 0);
        return 1;
    }

    uint16_t color = 0x0000; // 默认黑色
    if (lua_isinteger(L, 1)) {
        color = luaL_checkinteger(L, 1) & 0xFFFF;
    }

    // 分配临时缓冲区填充屏幕
    uint16_t *buffer = (uint16_t *)heap_caps_malloc(lcd_config.width * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (buffer == NULL) {
        LLOGE("Memory for bitmap is not enough");
        lua_pushboolean(L, 0);
        return 1;
    }

    // 填充缓冲区
    for (int i = 0; i < lcd_config.width; i++) {
        buffer[i] = color;
    }

    // 绘制每一行
    for (int y = 0; y < lcd_config.height; y++) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, lcd_config.width, y + 1, buffer);
    }

    heap_caps_free(buffer);
    lua_pushboolean(L, 1);
    return 1;
}

#include "rotable2.h"
static const rotable_Reg_t reg_qlcd[] = {
    { "init",           ROREG_FUNC(l_qlcd_init) },
    { "lvgl_register",  ROREG_FUNC(l_qlcd_lvgl_register) },
    { "clear",          ROREG_FUNC(l_qlcd_clear) },
    // 常量定义
    { "SPI_HOST_1",     ROREG_INT(SPI1_HOST) },
    { "SPI_HOST_2",     ROREG_INT(SPI2_HOST) },
    { "SPI_HOST_3",     ROREG_INT(SPI3_HOST) },
    { NULL,             ROREG_INT(0) }
};

LUAMOD_API int luaopen_qlcd(lua_State *L) {
    luat_newlib2(L, reg_qlcd);
    return 1;
}