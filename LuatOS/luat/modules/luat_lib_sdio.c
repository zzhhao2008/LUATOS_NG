
/*
@module  sdio
@summary sdio
@version 1.0
@date    2021.09.02
@tag LUAT_USE_SDIO
@usage
-- 本sdio库挂载tf卡到文件系统功能已经被fatfs的sdio模式取代
-- 本sdio库仅保留直接读写TF卡的函数
-- 例如 使用 sdio 0 挂载TF卡
fatfs.mount(fatfs.SDIO, "/sd", 0)

-- 挂载完成后, 使用 io 库的相关函数来操作就行
local f = io.open("/sd/abc.txt")
*/
#include "luat_base.h"
#include "luat_sdio.h"
#include "luat_mem.h"

#define SDIO_COUNT 2
static luat_sdio_t sdio_t[SDIO_COUNT];

/**
初始化sdio
@api sdio.init(id)
@int 通道id,与具体设备有关,通常从0开始,默认0
@return boolean 打开结果
 */
static int l_sdio_init(lua_State *L) {
    if (luat_sdio_init(luaL_optinteger(L, 1, 0)) == 0) {
        lua_pushboolean(L, 1);
    }
    else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

/*
直接读写sd卡上的数据
@api sdio.sd_read(id, offset, len)
@int sdio总线id
@int 偏移量,必须是512的倍数
@int 长度,必须是512的倍数
@return string 若读取成功,返回字符串,否则返回nil
@usage
-- 初始化sdio并直接读取sd卡数据
sdio.init(0)
local t = sdio.sd_read(0, 0, 1024)
if t then
    --- xxx
end
*/
static int l_sdio_read(lua_State *L) {
    int id = luaL_checkinteger(L, 1);
    int offset = luaL_checkinteger(L, 2);
    int len = luaL_checkinteger(L, 3);
    char* recv_buff = luat_heap_malloc(len);
    if(recv_buff == NULL)
        return 0;
    int ret = luat_sdio_sd_read(id, sdio_t[id].rca, recv_buff, offset, len);
    if (ret > 0) {
        lua_pushlstring(L, recv_buff, ret);
        luat_heap_free(recv_buff);
        return 1;
    }
    luat_heap_free(recv_buff);
    return 0;
}

/*
直接写sd卡
@api sdio.sd_write(id, data, offset)
@int sdio总线id
@string 待写入的数据,长度必须是512的倍数
@int 偏移量,必须是512的倍数
@return bool 若读取成功,返回true,否则返回false
@usage
-- 初始化sdio并直接读取sd卡数据
sdio.init(0)
local t = sdio.sd_write(0, data, 0)
if t then
    --- xxx
end
*/
static int l_sdio_write(lua_State *L) {
    int id = luaL_checkinteger(L, 1);
    size_t len;
    const char* send_buff;
    send_buff = lua_tolstring(L, 2, &len);
    int offset = luaL_checkinteger(L, 3);
    int ret = luat_sdio_sd_write(id, sdio_t[id].rca, (char*)send_buff, offset, len);
    if (ret > 0) {
        lua_pushboolean(L, 1);
    }
    lua_pushboolean(L, 0);
    return 1;
}

/*
设置SDIO的GPIO引脚配置
@api sdio.set_gpio_config(id, config)
@int SDIO总线id,与具体设备有关,通常从0开始
@table GPIO配置表,包含以下字段:
        - clk_gpio: 时钟引脚 (必需)
        - cmd_gpio: 命令引脚 (必需)
        - d0_gpio: 数据0引脚 (必需)
        - d1_gpio: 数据1引脚 (4位模式,可选)
        - d2_gpio: 数据2引脚 (4位模式,可选)
        - d3_gpio: 数据3引脚 (4位模式,可选)
        - cd_gpio: 卡检测引脚 (可选,默认-1不使用)
        - wp_gpio: 写保护引脚 (可选,默认-1不使用)
@return boolean 成功返回true,否则返回false
@return string 失败原因
@usage
-- 设置自定义GPIO配置 (必须在初始化前调用)
local gpio_config = {
    clk_gpio = 36,
    cmd_gpio = 35,
    d0_gpio = 37,
    d1_gpio = 38,
    d2_gpio = 33,
    d3_gpio = 34,
    cd_gpio = -1,
    wp_gpio = -1
}
local ok, err = sdio.set_gpio_config(0, gpio_config)
if ok then
    -- 初始化SDIO
    sdio.init(0)
else
    log.error("sdio", "set_gpio_config failed:", err)
end
*/
static int l_sdio_set_gpio_config(lua_State *L) {
    int id = luaL_checkinteger(L, 1);

    // 检查第一个参数是否为表
    if (!lua_istable(L, 2)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "config must be a table");
        return 2;
    }

    luat_sdio_gpio_config_t config;

    // 初始化配置为默认值
    config.clk_gpio = -1;
    config.cmd_gpio = -1;
    config.d0_gpio = -1;
    config.d1_gpio = -1;
    config.d2_gpio = -1;
    config.d3_gpio = -1;
    config.cd_gpio = -1;
    config.wp_gpio = -1;

    // 读取GPIO配置表中的值
    lua_getfield(L, 2, "clk_gpio");
    if (lua_isinteger(L, -1)) {
        config.clk_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "cmd_gpio");
    if (lua_isinteger(L, -1)) {
        config.cmd_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "d0_gpio");
    if (lua_isinteger(L, -1)) {
        config.d0_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "d1_gpio");
    if (lua_isinteger(L, -1)) {
        config.d1_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "d2_gpio");
    if (lua_isinteger(L, -1)) {
        config.d2_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "d3_gpio");
    if (lua_isinteger(L, -1)) {
        config.d3_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "cd_gpio");
    if (lua_isinteger(L, -1)) {
        config.cd_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "wp_gpio");
    if (lua_isinteger(L, -1)) {
        config.wp_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    // 验证必需的引脚
    if (config.clk_gpio < 0 || config.cmd_gpio < 0 || config.d0_gpio < 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "clk_gpio, cmd_gpio, and d0_gpio are required");
        return 2;
    }

    // 调用底层设置函数
    int ret = luat_sdio_set_gpio_config(id, &config);

    if (ret == 0) {
        lua_pushboolean(L, 1);
        return 1;
    } else {
        lua_pushboolean(L, 0);
        const char* err_msg = NULL;
        switch (ret) {
            case -1: err_msg = "Invalid SDIO ID"; break;
            case -2: err_msg = "Cannot set GPIO config after initialization"; break;
            case -3: err_msg = "Failed to allocate memory for GPIO configuration"; break;
            default: err_msg = "Unknown error"; break;
        }
        lua_pushstring(L, err_msg);
        return 2;
    }
}

/*
使用自定义GPIO配置初始化SDIO (一步设置并初始化)
@api sdio.init_with_gpio(id, config)
@int SDIO总线id,与具体设备有关,通常从0开始
@table GPIO配置表,格式同set_gpio_config
@return boolean 成功返回true,否则返回false
@return string 失败原因
@usage
-- 使用自定义GPIO配置初始化SDIO
local gpio_config = {
    clk_gpio = 36,
    cmd_gpio = 35,
    d0_gpio = 37,
    d1_gpio = 38,
    d2_gpio = 33,
    d3_gpio = 34
}
local ok, err = sdio.init_with_gpio(0, gpio_config)
if ok then
    log.info("sdio", "initialized successfully")
else
    log.error("sdio", "init failed:", err)
end
*/
static int l_sdio_init_with_gpio(lua_State *L) {
    int id = luaL_checkinteger(L, 1);

    // 检查第二个参数是否为表
    if (!lua_istable(L, 2)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "config must be a table");
        return 2;
    }

    luat_sdio_gpio_config_t config;

    // 初始化配置为默认值
    config.clk_gpio = -1;
    config.cmd_gpio = -1;
    config.d0_gpio = -1;
    config.d1_gpio = -1;
    config.d2_gpio = -1;
    config.d3_gpio = -1;
    config.cd_gpio = -1;
    config.wp_gpio = -1;

    // 读取GPIO配置表中的值
    lua_getfield(L, 2, "clk_gpio");
    if (lua_isinteger(L, -1)) {
        config.clk_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "cmd_gpio");
    if (lua_isinteger(L, -1)) {
        config.cmd_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "d0_gpio");
    if (lua_isinteger(L, -1)) {
        config.d0_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "d1_gpio");
    if (lua_isinteger(L, -1)) {
        config.d1_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "d2_gpio");
    if (lua_isinteger(L, -1)) {
        config.d2_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "d3_gpio");
    if (lua_isinteger(L, -1)) {
        config.d3_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "cd_gpio");
    if (lua_isinteger(L, -1)) {
        config.cd_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "wp_gpio");
    if (lua_isinteger(L, -1)) {
        config.wp_gpio = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    // 验证必需的引脚
    if (config.clk_gpio < 0 || config.cmd_gpio < 0 || config.d0_gpio < 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "clk_gpio, cmd_gpio, and d0_gpio are required");
        return 2;
    }

    // 调用底层初始化函数
    int ret = luat_sdio_init_with_gpio(id, &config);

    if (ret == 0) {
        lua_pushboolean(L, 1);
        return 1;
    } else {
        lua_pushboolean(L, 0);
        const char* err_msg = NULL;
        switch (ret) {
            case -1: err_msg = "Invalid SDIO ID"; break;
            case -2: err_msg = "Failed to initialize SDMMC host"; break;
            case -3: err_msg = "Failed to initialize SD card"; break;
            case -4: err_msg = "Failed to allocate memory for SD card structure"; break;
            case -5: err_msg = "Failed to read SD card info"; break;
            default: err_msg = "Unknown error"; break;
        }
        lua_pushstring(L, err_msg);
        return 2;
    }
}

#include "rotable2.h"
static const rotable_Reg_t reg_sdio[] =
{
    { "init" ,          ROREG_FUNC(l_sdio_init )},
    { "set_gpio_config",  ROREG_FUNC(l_sdio_set_gpio_config)},
    { "init_with_gpio",  ROREG_FUNC(l_sdio_init_with_gpio)},
    { "sd_read" ,       ROREG_FUNC(l_sdio_read )},
    { "sd_write" ,      ROREG_FUNC(l_sdio_write)},
    // { "sd_mount" ,      ROREG_FUNC(l_sdio_sd_mount)},
    // { "sd_umount" ,     ROREG_FUNC(l_sdio_sd_umount)},
    // { "sd_format" ,     ROREG_FUNC(l_sdio_sd_format)},
	{ NULL,             ROREG_INT(0) }
};

LUAMOD_API int luaopen_sdio( lua_State *L ) {
    luat_newlib2(L, reg_sdio);
    return 1;
}
