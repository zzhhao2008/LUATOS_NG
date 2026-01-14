/*
QLCD液晶屏驱动 - Lua API封装
仅负责与Lua虚拟机的交互，调用qlcd.c中的核心函数
*/
#include "luat_base.h"
#include "luat_gpio.h"
#include "luat_rtos.h"
#include "esp_log.h"
#include "lvgl.h"
#include "qlcd.h"

#define LUAT_LOG_TAG "qlcd"
#include "luat_log.h"

/*
初始化QLCD液晶屏
@api qlcd.init(config)
@table config 配置参数
@int config.width 屏幕宽度，默认320
@int config.height 屏幕高度，默认240
@int config.spi_host SPI主机号，默认SPI3_HOST(2)
@int config.mosi_pin MOSI引脚，默认17
@int config.clk_pin CLK引脚，默认18
@int config.dc_pin DC引脚，默认15
@int config.rst_pin RST引脚，默认7
@int config.cs_pin CS引脚，默认6
@int config.freq SPI频率，默认40MHz
@int config.draw_buf_height 绘制缓冲区高度，默认20
@return boolean 是否成功
@usage
local success = qlcd.init({
    width = 320,
    height = 240,
    spi_host = 2,
    mosi_pin = 17,
    clk_pin = 18,
    dc_pin = 15,
    rst_pin = 7,
    cs_pin = 6,
    freq = 40000000,
    draw_buf_height = 40
})
*/
static int l_qlcd_init(lua_State *L) {
    qlcd_config_t config = {
        .width = 320,
        .height = 240,
        .spi_host = SPI3_HOST, // 默认SPI3_HOST
        .mosi_pin = 17,
        .clk_pin = 18,
        .dc_pin = 15,
        .rst_pin = 7,
        .cs_pin = 6,
        .freq = 40000000, // 40MHz
        .draw_buf_height = 20
    };
    
    if (lua_type(L, 1) == LUA_TTABLE) {
        // 从配置表读取参数
        lua_getfield(L, 1, "width");
        if (lua_isinteger(L, -1)) config.width = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "height");
        if (lua_isinteger(L, -1)) config.height = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "spi_host");
        if (lua_isinteger(L, -1)) config.spi_host = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "mosi_pin");
        if (lua_isinteger(L, -1)) config.mosi_pin = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "clk_pin");
        if (lua_isinteger(L, -1)) config.clk_pin = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "dc_pin");
        if (lua_isinteger(L, -1)) config.dc_pin = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "rst_pin");
        if (lua_isinteger(L, -1)) config.rst_pin = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "cs_pin");
        if (lua_isinteger(L, -1)) config.cs_pin = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "freq");
        if (lua_isinteger(L, -1)) config.freq = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "draw_buf_height");
        if (lua_isinteger(L, -1)) config.draw_buf_height = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    
    // 检查引脚是否有效
    if (config.mosi_pin < 0 || config.clk_pin < 0 || config.dc_pin < 0 || config.cs_pin < 0) {
        LLOGE("Invalid pin configuration");
        lua_pushboolean(L, 0);
        return 1;
    }
    
    esp_err_t ret = qlcd_init(&config);
    if (ret != ESP_OK) {
        LLOGE("QLCD init failed: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }
    
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
    qlcd_state_t *state = qlcd_get_state();
    if (!state->is_initialized) {
        LLOGE("QLCD not initialized");
        lua_pushboolean(L, 0);
        return 1;
    }
    
    esp_err_t ret = qlcd_lvgl_register();
    if (ret != ESP_OK) {
        LLOGE("LVGL register failed: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }
    
    LLOGI("QLCD registered with LVGL successfully");
    lua_pushboolean(L, 1);
    return 1;
}

/*
LCD进入休眠模式
@api qlcd.sleep()
@return boolean 是否成功
@usage
qlcd.sleep()
*/
static int l_qlcd_sleep(lua_State *L) {
    qlcd_state_t *state = qlcd_get_state();
    if (!state->is_initialized) {
        LLOGE("QLCD not initialized");
        lua_pushboolean(L, 0);
        return 1;
    }
    
    esp_err_t ret = qlcd_sleep();
    if (ret != ESP_OK) {
        LLOGE("Sleep failed: %s", esp_err_to_name(ret));
    }
    lua_pushboolean(L, ret == ESP_OK);
    return 1;
}

/*
LCD唤醒
@api qlcd.wakeup()
@return boolean 是否成功
@usage
qlcd.wakeup()
*/
static int l_qlcd_wakeup(lua_State *L) {
    qlcd_state_t *state = qlcd_get_state();
    if (!state->is_initialized) {
        LLOGE("QLCD not initialized");
        lua_pushboolean(L, 0);
        return 1;
    }
    
    esp_err_t ret = qlcd_wakeup();
    if (ret != ESP_OK) {
        LLOGE("Wakeup failed: %s", esp_err_to_name(ret));
    }
    lua_pushboolean(L, ret == ESP_OK);
    return 1;
}

/*
清屏
@api qlcd.clear(color)
@int color 清屏颜色，默认为黑色
@usage
qlcd.clear(0xFFFF) -- 白色清屏
*/
static int l_qlcd_clear(lua_State *L) {
    qlcd_state_t *state = qlcd_get_state();
    if (!state->is_initialized) {
        LLOGE("QLCD not initialized");
        lua_pushboolean(L, 0);
        return 1;
    }
    
    uint16_t color = 0x0000; // 默认黑色
    if (lua_isinteger(L, 1)) {
        color = luaL_checkinteger(L, 1) & 0xFFFF;
    }
    
    qlcd_set_color(color);
    lua_pushboolean(L, 1);
    return 1;
}

/*
清理资源
@api qlcd.cleanup()
@usage
qlcd.cleanup()
*/
static int l_qlcd_cleanup(lua_State *L) {
    esp_err_t ret = qlcd_cleanup();
    if (ret != ESP_OK) {
        LLOGE("Cleanup failed: %s", esp_err_to_name(ret));
    }
    lua_pushboolean(L, ret == ESP_OK);
    return 1;
}

#include "rotable2.h"
static const rotable_Reg_t reg_qlcd[] = {
    { "init",           ROREG_FUNC(l_qlcd_init) },
    { "lvgl_register",  ROREG_FUNC(l_qlcd_lvgl_register) },
    { "sleep",          ROREG_FUNC(l_qlcd_sleep) },
    { "wakeup",         ROREG_FUNC(l_qlcd_wakeup) },
    { "clear",          ROREG_FUNC(l_qlcd_clear) },
    { "cleanup",        ROREG_FUNC(l_qlcd_cleanup) },
    // 颜色常量
    { "BLACK",          ROREG_INT(0x0000) },
    { "WHITE",          ROREG_INT(0xFFFF) },
    { "RED",            ROREG_INT(0xF800) },
    { "GREEN",          ROREG_INT(0x07E0) },
    { "BLUE",           ROREG_INT(0x001F) },
    { NULL,             ROREG_INT(0) }
};

LUAMOD_API int luaopen_qlcd(lua_State *L) {
    luat_newlib2(L, reg_qlcd);
    return 1;
}