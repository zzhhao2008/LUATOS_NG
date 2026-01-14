#pragma once
#include <string.h>
#include "math.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_types.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// LCD配置结构
typedef struct {
    int width;
    int height;
    int spi_host;
    int mosi_pin;
    int clk_pin;
    int dc_pin;
    int rst_pin;
    int cs_pin;
    int freq;
    int draw_buf_height;
} qlcd_config_t;

// QLCD状态结构
typedef struct {
    bool is_initialized;
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_panel_io_handle_t io_handle;
    spi_host_device_t spi_host;
    qlcd_config_t config;
} qlcd_state_t;

// QLCD相关函数声明
esp_err_t qlcd_init(const qlcd_config_t *config);
esp_err_t qlcd_lvgl_register(void);
void qlcd_set_color(uint16_t color);
void qlcd_draw_picture(int x_start, int y_start, int x_end, int y_end, const unsigned char *gImage);
esp_err_t qlcd_sleep(void);     // 进入休眠模式
esp_err_t qlcd_wakeup(void);    // 唤醒
esp_err_t qlcd_cleanup(void);   // 清理资源

// 获取QLCD状态
qlcd_state_t* qlcd_get_state(void);

#ifdef __cplusplus
}
#endif