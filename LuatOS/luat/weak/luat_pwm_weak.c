#include "luat_base.h"
#include "luat_pwm.h"

LUAT_WEAK int luat_pwm_setup(luat_pwm_conf_t* conf) {
    return luat_pwm_open(conf->channel, conf->period, conf->pulse, conf->pnum);
}

LUAT_WEAK int luat_pwm_fade(int channel, int target_duty, int time_ms) {
    // 默认实现返回错误，表示不支持fade功能
    return -1;
}

LUAT_WEAK int luat_pwm_stop_fade(int channel) {
    // 默认实现返回错误，表示不支持fade功能
    return -1;
}