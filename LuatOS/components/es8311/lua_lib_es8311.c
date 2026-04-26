/*
@module  es8311
@summary ES8311音频编解码器驱动
@version 1.0
@date    2026.04.26
@usage
-- 初始化ES8311
local es8311 = require "es8311"
local success = es8311.init({
    sda = 10,
    scl = 9,
    mclk = 4,
    sclk = 5,
    asdout = 46,
    lrck = 12,
    dsin = 45,
    i2c_id = 0,
    fre = 100000
})
if not success then
    log.error("es8311", "init failed")
    return
end

-- 设置为播放模式，采样率44100Hz
es8311.set_sample_rate(44100)
es8311.set_mode(es8311.MODE_DAC)
es8311.set_volume(80)

-- 开始播放
es8311.start(es8311.MODE_DAC)

-- 写入音频数据
es8311.write(audio_data, #audio_data)

-- ... 继续写入音频数据 ...

-- 停止播放
es8311.stop()
*/
#include "luat_base.h"
#include "luat_gpio.h"
#include "luat_rtos.h"
#include "esp_log.h"
#include "es8311_driver.h"

#define LUAT_LOG_TAG "es8311"
#include "luat_log.h"

#define ES8311_DEFAULT_FREQ 100000  // 100kHz

static es8311_cfg_t es8311_config = {0};

/*
初始化ES8311音频编解码器
@api es8311.init(config)
@table config 配置参数
@int config.sda SDA引脚，默认10
@int config.scl SCL引脚，默认9
@int config.mclk MCLK引脚，默认4
@int config.sclk SCLK引脚(I2S BCLK)，默认5
@int config.asdout ASDOUT引脚(I2S DAC输出)，默认46
@int config.lrck LRCK引脚(I2S LRCK)，默认12
@int config.dsin DSIN引脚(I2S ADC输入)，默认45
@int config.i2c_id I2C端口号，默认0
@int config.fre I2C频率，默认100000
@return boolean 是否成功
@usage
local success = es8311.init({
    sda = 10,
    scl = 9,
    i2c_id = 0
})
if success then
    log.info("es8311", "init ok")
end
*/
static int l_es8311_init(lua_State *L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        LLOGE("config must be a table");
        lua_pushboolean(L, 0);
        return 1;
    }

    // 初始化配置结构
    memset(&es8311_config, 0, sizeof(es8311_cfg_t));

    // 默认值
    es8311_config.i2c_num = I2C_NUM_0;

    // 读取SDA
    lua_getfield(L, 1, "sda");
    if (lua_isinteger(L, -1)) {
        es8311_config.sda = luaL_checkinteger(L, -1);
    } else {
        es8311_config.sda = GPIO_NUM_10; // 默认值
    }
    lua_pop(L, 1);

    // 读取SCL
    lua_getfield(L, 1, "scl");
    if (lua_isinteger(L, -1)) {
        es8311_config.scl = luaL_checkinteger(L, -1);
    } else {
        es8311_config.scl = GPIO_NUM_9; // 默认值
    }
    lua_pop(L, 1);

    // 读取MCLK
    lua_getfield(L, 1, "mclk");
    if (lua_isinteger(L, -1)) {
        es8311_config.mclk = luaL_checkinteger(L, -1);
    } else {
        es8311_config.mclk = GPIO_NUM_4; // 默认值
    }
    lua_pop(L, 1);

    // 读取SCLK
    lua_getfield(L, 1, "sclk");
    if (lua_isinteger(L, -1)) {
        es8311_config.sclk = luaL_checkinteger(L, -1);
    } else {
        es8311_config.sclk = GPIO_NUM_5; // 默认值
    }
    lua_pop(L, 1);

    // 读取ASDOUT
    lua_getfield(L, 1, "asdout");
    if (lua_isinteger(L, -1)) {
        es8311_config.asdout = luaL_checkinteger(L, -1);
    } else {
        es8311_config.asdout = GPIO_NUM_46; // 默认值
    }
    lua_pop(L, 1);

    // 读取LRCK
    lua_getfield(L, 1, "lrck");
    if (lua_isinteger(L, -1)) {
        es8311_config.lrck = luaL_checkinteger(L, -1);
    } else {
        es8311_config.lrck = GPIO_NUM_12; // 默认值
    }
    lua_pop(L, 1);

    // 读取DSIN
    lua_getfield(L, 1, "dsin");
    if (lua_isinteger(L, -1)) {
        es8311_config.dsin = luaL_checkinteger(L, -1);
    } else {
        es8311_config.dsin = GPIO_NUM_45; // 默认值
    }
    lua_pop(L, 1);

    // 读取I2C ID
    lua_getfield(L, 1, "i2c_id");
    if (lua_isinteger(L, -1)) {
        es8311_config.i2c_num = luaL_checkinteger(L, -1);
    }
    lua_pop(L, 1);

    // 读取I2C频率
    lua_getfield(L, 1, "fre");
    if (lua_isinteger(L, -1)) {
        es8311_config.fre = luaL_checkinteger(L, -1);
    } else {
        es8311_config.fre = ES8311_DEFAULT_FREQ;
    }
    lua_pop(L, 1);

    // 调用驱动初始化
    esp_err_t ret = es8311_init(&es8311_config);
    if (ret != ESP_OK) {
        LLOGE("ES8311 initialization failed: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }

    LLOGI("ES8311 initialized successfully");
    lua_pushboolean(L, 1);
    return 1;
}

/*
反初始化ES8311
@api es8311.deinit()
@return boolean 是否成功
@usage
es8311.deinit()
*/
static int l_es8311_deinit(lua_State *L) {
    esp_err_t ret = es8311_deinit();
    if (ret != ESP_OK) {
        LLOGE("ES8311 deinitialization failed: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }

    LLOGI("ES8311 deinitialized successfully");
    lua_pushboolean(L, 1);
    return 1;
}

/*
设置采样率
@api es8311.set_sample_rate(sample_rate)
@int sample_rate 采样率(8000, 16000, 32000, 44100, 48000)
@return boolean 是否成功
@usage
es8311.set_sample_rate(44100)
*/
static int l_es8311_set_sample_rate(lua_State *L) {
    if (lua_gettop(L) < 1) {
        LLOGE("sample_rate is required");
        lua_pushboolean(L, 0);
        return 1;
    }

    uint32_t sample_rate = luaL_checkinteger(L, 1);

    esp_err_t ret = es8311_set_sample_rate(sample_rate);
    if (ret != ESP_OK) {
        LLOGE("Failed to set sample rate: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/*
设置音频模式
@api es8311.set_mode(mode)
@int mode 音频模式 (0=DAC播放, 1=ADC录制, 2=同时录制和播放)
@return boolean 是否成功
@usage
es8311.set_mode(es8311.MODE_DAC)  -- 播放模式
es8311.set_mode(es8311.MODE_ADC)  -- 录制模式
es8311.set_mode(es8311.MODE_BOTH) -- 同时模式
*/
static int l_es8311_set_mode(lua_State *L) {
    if (lua_gettop(L) < 1) {
        LLOGE("mode is required");
        lua_pushboolean(L, 0);
        return 1;
    }

    es8311_audio_mode_t mode = (es8311_audio_mode_t)luaL_checkinteger(L, 1);

    esp_err_t ret = es8311_set_mode(mode);
    if (ret != ESP_OK) {
        LLOGE("Failed to set mode: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/*
设置音量
@api es8311.set_volume(volume)
@int volume 音量(0-100)
@return boolean 是否成功
@usage
es8311.set_volume(80)  -- 80%音量
*/
static int l_es8311_set_volume(lua_State *L) {
    if (lua_gettop(L) < 1) {
        LLOGE("volume is required");
        lua_pushboolean(L, 0);
        return 1;
    }

    uint8_t volume = luaL_checkinteger(L, 1);

    esp_err_t ret = es8311_set_volume(volume);
    if (ret != ESP_OK) {
        LLOGE("Failed to set volume: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/*
获取当前音量
@api es8311.get_volume()
@return number 音量(0-100)，失败返回nil
@usage
local vol = es8311.get_volume()
if vol then
    log.info("es8311", "volume", vol)
end
*/
static int l_es8311_get_volume(lua_State *L) {
    uint8_t volume;
    esp_err_t ret = es8311_get_volume(&volume);
    if (ret != ESP_OK) {
        LLOGE("Failed to get volume: %s", esp_err_to_name(ret));
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, volume);
    return 1;
}

/*
设置静音
@api es8311.set_mute(enable)
@boolean enable true静音，false取消静音
@return boolean 是否成功
@usage
es8311.set_mute(true)   -- 静音
es8311.set_mute(false)  -- 取消静音
*/
static int l_es8311_set_mute(lua_State *L) {
    if (lua_gettop(L) < 1) {
        LLOGE("enable is required");
        lua_pushboolean(L, 0);
        return 1;
    }

    uint8_t enable = lua_toboolean(L, 1);

    esp_err_t ret = es8311_set_mute(enable);
    if (ret != ESP_OK) {
        LLOGE("Failed to set mute: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/*
获取静音状态
@api es8311.get_mute()
@return boolean 静音状态，失败返回nil
@usage
local muted = es8311.get_mute()
if muted ~= nil then
    log.info("es8311", "muted", muted)
end
*/
static int l_es8311_get_mute(lua_State *L) {
    uint8_t enabled;
    esp_err_t ret = es8311_get_mute(&enabled);
    if (ret != ESP_OK) {
        LLOGE("Failed to get mute status: %s", esp_err_to_name(ret));
        lua_pushnil(L);
        return 1;
    }

    lua_pushboolean(L, enabled);
    return 1;
}

/*
获取芯片信息
@api es8311.get_info()
@return table 包含芯片ID和版本信息，失败返回nil
@usage
local info = es8311.get_info()
if info then
    log.info("es8311", "chip_id", info.chip_id)
    log.info("es8311", "version", info.version)
end
*/
static int l_es8311_get_info(lua_State *L) {
    uint8_t chip_id, version;
    esp_err_t ret = es8311_get_info(&chip_id, &version);
    if (ret != ESP_OK) {
        LLOGE("Failed to get chip info: %s", esp_err_to_name(ret));
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    lua_pushstring(L, "chip_id");
    lua_pushinteger(L, chip_id);
    lua_settable(L, -3);

    lua_pushstring(L, "version");
    lua_pushinteger(L, version);
    lua_settable(L, -3);

    return 1;
}

/*
开始音频播放/录制
@api es8311.start(mode)
@int mode 音频模式 (0=DAC播放, 1=ADC录制, 2=同时录制和播放)
@return boolean 是否成功
@usage
es8311.start(es8311.MODE_DAC)  -- 开始播放
es8311.start(es8311.MODE_ADC)  -- 开始录制
es8311.start(es8311.MODE_BOTH) -- 开始同时录制和播放
*/
static int l_es8311_start(lua_State *L) {
    if (lua_gettop(L) < 1) {
        LLOGE("mode is required");
        lua_pushboolean(L, 0);
        return 1;
    }

    es8311_audio_mode_t mode = (es8311_audio_mode_t)luaL_checkinteger(L, 1);

    esp_err_t ret = es8311_start(mode);
    if (ret != ESP_OK) {
        LLOGE("Failed to start: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/*
停止音频播放/录制
@api es8311.stop()
@return boolean 是否成功
@usage
es8311.stop()
*/
static int l_es8311_stop(lua_State *L) {
    esp_err_t ret = es8311_stop();
    if (ret != ESP_OK) {
        LLOGE("Failed to stop: %s", esp_err_to_name(ret));
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/*
写入音频数据(用于播放)
@api es8311.write(data)
@string/data data 音频数据
@return number 实际写入的字节数，失败返回nil
@usage
local written = es8311.write(audio_data)
if written then
    log.info("es8311", "written", written)
end
*/
static int l_es8311_write(lua_State *L) {
    if (lua_gettop(L) < 1) {
        LLOGE("data is required");
        lua_pushnil(L);
        return 1;
    }

    const char *data;
    size_t len;

    if (lua_isstring(L, 1)) {
        data = luaL_checklstring(L, 1, &len);
    } else {
        LLOGE("data must be a string");
        lua_pushnil(L);
        return 1;
    }

    esp_err_t ret = es8311_write_audio((uint8_t*)data, len);
    if (ret != ESP_OK) {
        LLOGE("Failed to write audio: %s", esp_err_to_name(ret));
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, len);
    return 1;
}

/*
读取音频数据(用于录制)
@api es8311.read(len)
@int len 期望读取的长度
@return string 音频数据，失败返回nil
@usage
local audio_data = es8311.read(1024)
if audio_data then
    log.info("es8311", "received", #audio_data)
    -- 处理音频数据
end
*/
static int l_es8311_read(lua_State *L) {
    if (lua_gettop(L) < 1) {
        LLOGE("len is required");
        lua_pushnil(L);
        return 1;
    }

    size_t len = luaL_checkinteger(L, 1);

    // 分配缓冲区
    uint8_t *buffer = (uint8_t*)malloc(len);
    if (buffer == NULL) {
        LLOGE("Failed to allocate buffer");
        lua_pushnil(L);
        return 1;
    }

    size_t actual_len = 0;
    esp_err_t ret = es8311_read_audio(buffer, len, &actual_len);
    if (ret != ESP_OK) {
        LLOGE("Failed to read audio: %s", esp_err_to_name(ret));
        free(buffer);
        lua_pushnil(L);
        return 1;
    }

    // 返回Lua字符串
    lua_pushlstring(L, (const char*)buffer, actual_len);
    free(buffer);

    return 1;
}

#include "rotable2.h"
static const rotable_Reg_t reg_es8311[] = {
    { "init",            ROREG_FUNC(l_es8311_init) },
    { "deinit",          ROREG_FUNC(l_es8311_deinit) },
    { "set_sample_rate", ROREG_FUNC(l_es8311_set_sample_rate) },
    { "set_mode",        ROREG_FUNC(l_es8311_set_mode) },
    { "set_volume",      ROREG_FUNC(l_es8311_set_volume) },
    { "get_volume",      ROREG_FUNC(l_es8311_get_volume) },
    { "set_mute",       ROREG_FUNC(l_es8311_set_mute) },
    { "get_mute",       ROREG_FUNC(l_es8311_get_mute) },
    { "get_info",        ROREG_FUNC(l_es8311_get_info) },
    { "start",           ROREG_FUNC(l_es8311_start) },
    { "stop",            ROREG_FUNC(l_es8311_stop) },
    { "write",           ROREG_FUNC(l_es8311_write) },
    { "read",            ROREG_FUNC(l_es8311_read) },
    // 常量
    { "MODE_DAC",        ROREG_INT(0) },        // 仅DAC播放
    { "MODE_ADC",        ROREG_INT(1) },        // 仅ADC录制
    { "MODE_BOTH",       ROREG_INT(2) },       // 同时播放和录制
    { "I2C_FREQ_100K",   ROREG_INT(100000) },
    { "I2C_FREQ_400K",   ROREG_INT(400000) },
    { "SAMPLE_RATE_8K",   ROREG_INT(8000) },
    { "SAMPLE_RATE_16K",  ROREG_INT(16000) },
    { "SAMPLE_RATE_32K",  ROREG_INT(32000) },
    { "SAMPLE_RATE_44K",  ROREG_INT(44100) },
    { "SAMPLE_RATE_48K",  ROREG_INT(48000) },
    { NULL,              ROREG_INT(0) }
};

LUAMOD_API int luaopen_es8311(lua_State *L) {
    luat_newlib2(L, reg_es8311);
    return 1;
}
