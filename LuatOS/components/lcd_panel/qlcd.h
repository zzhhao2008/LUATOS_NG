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
#include "driver/gpio.h"  // 添加GPIO_NUM_NC定义

// QLCD相关函数声明
esp_err_t qlcd_init(int width, int height, int spi_host, int mosi_pin, int clk_pin, int dc_pin, int rst_pin, int cs_pin, int bl_pin, int freq, int draw_buf_height);
esp_err_t qlcd_lvgl_register(void);
void qlcd_set_color(uint16_t color);
void qlcd_draw_pictrue(int x_start, int y_start, int x_end, int y_end, const unsigned char *gImage);