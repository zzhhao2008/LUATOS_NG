#ifndef _FT6336U_DRIVER_H_
#define _FT6336U_DRIVER_H_

#include "driver/gpio.h"
#include "esp_err.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "lvgl.h"

#define FT6336U_I2C_NUM I2C_NUM_0
#define FT6336U_ADDR    0x38

// FT6336U 触摸IC驱动
typedef struct {
    gpio_num_t scl;        // SCL管脚
    gpio_num_t sda;        // SDA管脚
    gpio_num_t rst;        // RST管脚(可选)，使用-1表示不使用
    gpio_num_t int_pin;    // INT管脚(可选)，使用-1表示不使用
    uint32_t fre;          // I2C速率
    uint16_t x_limit;      // X方向触摸边界
    uint16_t y_limit;      // Y方向触摸边界
    bool swapxy;           // 是否交换XY坐标
    bool mirror_x;         // 是否镜像X坐标
    bool mirror_y;         // 是否镜像Y坐标
} ft6336u_cfg_t;

/** ft6336u初始化
* @param cfg 配置
* @return err
*/
esp_err_t ft6336u_init(ft6336u_cfg_t *cfg);

/** 读取坐标值
* @param  x x坐标
* @param  y y坐标
* @param state 松手状态 0,松手 1按下
* @return 无
*/
void ft6336u_read(int16_t *x, int16_t *y, int *state);

/** 获取设备信息
* @param chip_id 芯片ID
* @param firmware_version 固件版本
* @param vendor_id 供应商ID
* @return esp_err_t
*/
esp_err_t ft6336u_get_info(uint8_t *chip_id, uint8_t *firmware_version, uint8_t *vendor_id);

#ifdef __cplusplus
extern "C" {
#endif
lv_indev_t *ft6336u_lvgl_init(ft6336u_cfg_t *cfg);
void ft6336u_lvgl_get_state(int16_t *x, int16_t *y, uint8_t *state);
#ifdef __cplusplus
}
#endif

#endif