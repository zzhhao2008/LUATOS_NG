/*
@module  ft6336u
@summary FT6336U电容触摸屏驱动
@version 1.0
@date    2026.01.09
@demo ft6336u
@usage
-- 初始化触摸屏
local ft6336u = require "ft6336u"
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

-- 将触摸屏注册到LVGL
ft6336u.lvgl_register()

-- 或者手动读取触摸状态
local x, y, state = ft6336u.read()
log.info("touch", x, y, state)
*/
#include "luat_base.h"
#include "luat_gpio.h"
#include "luat_rtos.h"
#include "esp_log.h"
#include "ft6336u_driver.h"
#include "lvgl.h"

#define LUAT_LOG_TAG "ft6336u"
#include "luat_log.h"

static ft6336u_cfg_t touch_config = {0};

#define FT6336U_DEFAULT_FREQ 400000  // 400kHz

/*
初始化ft6336u触摸屏
@api ft6336u.init(config)
@table config 配置参数
@int config.sda SDA引脚
@int config.scl SCL引脚  
@int config.rst RST引脚(可选)
@int config.int INT引脚(可选)
@int config.fre I2C频率, 默认400000
@int config.x_limit X轴最大值
@int config.y_limit Y轴最大值
@bool config.swapxy 是否交换XY坐标
@bool config.mirror_x 是否镜像X坐标
@bool config.mirror_y 是否镜像Y坐标
@return boolean 是否成功
@usage
local success = ft6336u.init({
    sda = 11,
    scl = 9,
    x_limit = 480,
    y_limit = 320
})
*/
static int l_ft6336u_init(lua_State *L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        LLOGE("config must be a table");
        return 0;
    }
    
    // 初始化配置结构
    memset(&touch_config, 0, sizeof(ft6336u_cfg_t));
    
    // 读取SDA
    lua_getfield(L, 1, "sda");
    if (lua_isinteger(L, -1)) {
        touch_config.sda = luaL_checkinteger(L, -1);
    } else {
        LLOGE("sda pin not specified");
        lua_pop(L, 1);
        return 0;
    }
    lua_pop(L, 1);
    
    // 读取SCL
    lua_getfield(L, 1, "scl");
    if (lua_isinteger(L, -1)) {
        touch_config.scl = luaL_checkinteger(L, -1);
    } else {
        LLOGE("scl pin not specified");
        lua_pop(L, 1);
        return 0;
    }
    lua_pop(L, 1);
    
    // 读取RST (可选)
    lua_getfield(L, 1, "rst");
    if (lua_isinteger(L, -1)) {
        touch_config.rst = luaL_checkinteger(L, -1);
        // 配置RST引脚
        gpio_set_direction(touch_config.rst, GPIO_MODE_OUTPUT);
        gpio_set_level(touch_config.rst, 0);  // 默认拉低
        luat_rtos_task_sleep(10);
        gpio_set_level(touch_config.rst, 1);  // 拉高复位
    }
    lua_pop(L, 1);
    
    // 读取INT (可选)
    lua_getfield(L, 1, "int");
    if (lua_isinteger(L, -1)) {
        // 配置INT引脚为输入，上拉
        touch_config.int_pin = luaL_checkinteger(L, -1);
        gpio_set_direction(touch_config.int_pin, GPIO_MODE_INPUT);
        gpio_pullup_en(touch_config.int_pin);
    }
    lua_pop(L, 1);
    
    // 读取频率
    lua_getfield(L, 1, "fre");
    if (lua_isinteger(L, -1)) {
        touch_config.fre = luaL_checkinteger(L, -1);
    } else {
        touch_config.fre = FT6336U_DEFAULT_FREQ;
    }
    lua_pop(L, 1);
    
    // 读取坐标限制
    lua_getfield(L, 1, "x_limit");
    if (lua_isinteger(L, -1)) {
        touch_config.x_limit = luaL_checkinteger(L, -1);
    } else {
        touch_config.x_limit = 480;  // 默认值
    }
    lua_pop(L, 1);
    
    lua_getfield(L, 1, "y_limit");
    if (lua_isinteger(L, -1)) {
        touch_config.y_limit = luaL_checkinteger(L, -1);
    } else {
        touch_config.y_limit = 320;  // 默认值
    }
    lua_pop(L, 1);
    
    // 读取坐标转换配置
    lua_getfield(L, 1, "swapxy");
    touch_config.swapxy = lua_toboolean(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 1, "mirror_x");
    touch_config.mirror_x = lua_toboolean(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, 1, "mirror_y");
    touch_config.mirror_y = lua_toboolean(L, -1);
    lua_pop(L, 1);
    
    // 调用驱动初始化
    esp_err_t ret = ft6336u_init(&touch_config);
    if (ret != ESP_OK) {
        LLOGE("FT6336U initialization failed");
        lua_pushboolean(L, 0);
        return 1;
    }
    
    LLOGI("FT6336U initialized successfully");
    lua_pushboolean(L, 1);
    return 1;
}

/*
读取触摸状态
@api ft6336u.read()
@return int x坐标
@return int y坐标
@return int 触摸状态(0=无触摸, 1=触摸)
@usage
local x, y, state = ft6336u.read()
if state == 1 then
    log.info("touch at", x, y)
end
*/
static int l_ft6336u_read(lua_State *L) {
    int16_t x = 0, y = 0;
    int state = 0;
    
    ft6336u_read(&x, &y, &state);
    
    // 根据配置进行坐标转换
    if (touch_config.mirror_x) {
        x = touch_config.x_limit - 1 - x;
    }
    if (touch_config.mirror_y) {
        y = touch_config.y_limit - 1 - y;
    }
    if (touch_config.swapxy) {
        int16_t temp = x;
        x = y;
        y = temp;
    }
    
    // 边界检查
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= touch_config.x_limit) x = touch_config.x_limit - 1;
    if (y >= touch_config.y_limit) y = touch_config.y_limit - 1;
    
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, state);
    return 3;
}

/*
将触摸屏注册到LVGL
@api ft6336u.lvgl_register()
@return boolean 是否成功
@usage
ft6336u.lvgl_register()
*/
static int l_ft6336u_lvgl_register(lua_State *L) {
    lv_indev_t *indev = ft6336u_lvgl_init(&touch_config);
    if (indev == NULL) {
        LLOGE("Failed to register FT6336U with LVGL");
        lua_pushboolean(L, 0);
        return 1;
    }
    
    LLOGI("FT6336U registered with LVGL successfully");
    lua_pushboolean(L, 1);
    return 1;
}

/*
获取触摸屏信息
@api ft6336u.info()
@return table 包含芯片ID、固件版本等信息
@usage
local info = ft6336u.info()
log.info("touch info", info.chip_id, info.firmware_version)
*/
static int l_ft6336u_info(lua_State *L) {
    uint8_t chip_id, firmware_version, vendor_id;
    
    // 读取芯片ID
    uint8_t write_buf[1];
    uint8_t read_buf[1];
    
    write_buf[0] = 0xA3;
    if (i2c_master_transmit_receive(ft6336u_i2c_device, write_buf, 1, read_buf, 1, 200) == ESP_OK) {
        chip_id = read_buf[0];
    } else {
        chip_id = 0;
    }
    
    write_buf[0] = 0xA6;
    if (i2c_master_transmit_receive(ft6336u_i2c_device, write_buf, 1, read_buf, 1, 200) == ESP_OK) {
        firmware_version = read_buf[0];
    } else {
        firmware_version = 0;
    }
    
    write_buf[0] = 0xA8;
    if (i2c_master_transmit_receive(ft6336u_i2c_device, write_buf, 1, read_buf, 1, 200) == ESP_OK) {
        vendor_id = read_buf[0];
    } else {
        vendor_id = 0;
    }
    
    lua_newtable(L);
    
    lua_pushstring(L, "chip_id");
    lua_pushinteger(L, chip_id);
    lua_settable(L, -3);
    
    lua_pushstring(L, "firmware_version");
    lua_pushinteger(L, firmware_version);
    lua_settable(L, -3);
    
    lua_pushstring(L, "vendor_id");
    lua_pushinteger(L, vendor_id);
    lua_settable(L, -3);
    
    lua_pushstring(L, "x_limit");
    lua_pushinteger(L, touch_config.x_limit);
    lua_settable(L, -3);
    
    lua_pushstring(L, "y_limit");
    lua_pushinteger(L, touch_config.y_limit);
    lua_settable(L, -3);
    
    return 1;
}

#include "rotable2.h"
static const rotable_Reg_t reg_ft6336u[] = {
    { "init",           ROREG_FUNC(l_ft6336u_init) },
    { "read",           ROREG_FUNC(l_ft6336u_read) },
    { "lvgl_register",  ROREG_FUNC(l_ft6336u_lvgl_register) },
    { "info",           ROREG_FUNC(l_ft6336u_info) },
    
    // 常量
    { "I2C_FREQ_100K",  ROREG_INT(100000) },
    { "I2C_FREQ_400K",  ROREG_INT(400000) },
    { "I2C_FREQ_1M",    ROREG_INT(1000000) },
    
    { NULL,             ROREG_INT(0) }
};

LUAMOD_API int luaopen_ft6336u(lua_State *L) {
    luat_newlib2(L, reg_ft6336u);
    return 1;
}