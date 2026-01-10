/*
@module  pwm
@summary PWM模块
@version 1.0
@date    2020.07.03
@demo pwm
@tag LUAT_USE_PWM
*/
#include "luat_base.h"
#include "luat_pwm.h"
/**
开启指定的PWM通道
@api pwm.open(channel, period, pulse, pnum, precision, fade_time, target_pulse)
@int PWM通道
@int 频率, 1-1000000hz
@int 占空比 0-分频精度
@int 输出周期 0为持续输出, 1为单次输出, 其他为指定脉冲数输出
@int 分频精度, 100/256/1000, 默认为100, 若设备不支持会有日志提示
@int[opt=0] 过渡时间(毫秒)，0表示不使用平滑过渡
@int[opt=-1] 目标占空比，仅当fade_time>0时有效
@return boolean 处理结果,成功返回true,失败返回false
@usage
-- 打开PWM5, 频率1kHz, 占空比50%
pwm.open(5, 1000, 50)
-- 打开PWM5, 频率10kHz, 分频为 31/256
pwm.open(5, 10000, 31, 0, 256)
-- 打开PWM5, 频率1kHz, 从当前占空比过渡到80%，耗时1000ms
pwm.open(5, 1000, 50, 0, 100, 1000, 80)
*/
static int l_pwm_open(lua_State *L) {
    luat_pwm_conf_t conf = {
        .pnum = 0,
        .precision = 100,
        .fade_time = 0,
        .target_pulse = -1
    };
    conf.channel = luaL_checkinteger(L, 1);
    conf.period = luaL_checkinteger(L, 2);
    conf.pulse = luaL_optnumber(L, 3, 0);
    if (lua_isnumber(L, 4) || lua_isinteger(L, 4)){
        conf.pnum = luaL_checkinteger(L, 4);
    }
    if (lua_isnumber(L, 5) || lua_isinteger(L, 5)){
        conf.precision = luaL_checkinteger(L, 5);
    }
    // 检查是否有fade参数
    if (lua_gettop(L) >= 7) {
        conf.fade_time = luaL_checkinteger(L, 6);
        conf.target_pulse = luaL_checkinteger(L, 7);
    }
    int ret = luat_pwm_setup(&conf);
    lua_pushboolean(L, ret == 0 ? 1 : 0);
    return 1;
}

/**
设置PWM占空比平滑过渡
@api pwm.fade(channel, target_duty, time_ms)
@int PWM通道
@int 目标占空比 (0-分频精度)
@int 过渡时间(毫秒)
@return boolean 处理结果,成功返回true,失败返回false
@usage
-- PWM5从当前占空比平滑过渡到80%，耗时1000ms
pwm.fade(5, 80, 1000)
*/
static int l_pwm_fade(lua_State *L) {
    int channel = luaL_checkinteger(L, 1);
    int target_duty = luaL_checkinteger(L, 2);
    int time_ms = luaL_checkinteger(L, 3);
    
    int ret = luat_pwm_fade(channel, target_duty, time_ms);
    lua_pushboolean(L, ret == 0 ? 1 : 0);
    return 1;
}

/**
停止PWM占空比过渡
@api pwm.stop_fade(channel)
@int PWM通道
@return boolean 处理结果,成功返回true,失败返回false
@usage
-- 停止PWM5的过渡
pwm.stop_fade(5)
*/
static int l_pwm_stop_fade(lua_State *L) {
    int channel = luaL_checkinteger(L, 1);
    int ret = luat_pwm_stop_fade(channel);
    lua_pushboolean(L, ret == 0 ? 1 : 0);
    return 1;
}

/**
关闭指定的PWM通道
@api pwm.close(channel)
@int PWM通道
@return nil 无处理结果
@usage
-- 关闭PWM5
pwm.close(5)
*/
static int l_pwm_close(lua_State *L) {
    luat_pwm_close(luaL_checkinteger(L, 1));
    return 0;
}

/**
PWM捕获
@api pwm.capture(channel)
@int PWM通道
@int 捕获频率
@return boolean 处理结果,成功返回true,失败返回false
@usage
-- PWM0捕获
while 1 do
    pwm.capture(0,1000)
    local ret,channel,pulse,pwmH,pwmL  = sys.waitUntil("PWM_CAPTURE", 2000)
    if ret then
        log.info("PWM_CAPTURE","channel"..channel,"pulse"..pulse,"pwmH"..pwmH,"pwmL"..pwmL)
    end
end
*/
static int l_pwm_capture(lua_State *L) {
    int ret = luat_pwm_capture(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2));
    lua_pushboolean(L, ret == 0 ? 1 : 0);
    return 1;
}

#include "rotable2.h"
static const rotable_Reg_t reg_pwm[] =
{
    { "open" ,       ROREG_FUNC(l_pwm_open )},
    { "close" ,      ROREG_FUNC(l_pwm_close)},
    { "capture" ,    ROREG_FUNC(l_pwm_capture)},
    { "fade" ,       ROREG_FUNC(l_pwm_fade)},
    { "stop_fade" ,  ROREG_FUNC(l_pwm_stop_fade)},
    { NULL,          ROREG_INT(0) }
};
LUAMOD_API int luaopen_pwm( lua_State *L ) {
    luat_newlib2(L, reg_pwm);
    return 1;
}