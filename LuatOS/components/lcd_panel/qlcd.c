#include "qlcd.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "string.h"
#include "esp_log.h"

/**
 * Build for ESP32S3 / C3
 * ESPIDF v5.1.6
 * LVGL 7.11
 */
static const char *TAG = "QLCD";

// 修复：显式定义LV_CLAMP宏，解决隐式声明错误
#ifndef LV_CLAMP
#define LV_CLAMP(min, val, max) ((val) < (min) ? (min) : ((val) > (max)) ? (max) : (val))
#endif

// QLCD状态实例
static qlcd_state_t qlcd_state = {
    .is_initialized = false,
    .panel_handle = NULL,
    .io_handle = NULL,
    .spi_host = SPI3_HOST};

// 获取QLCD状态
qlcd_state_t *qlcd_get_state(void)
{
    return &qlcd_state;
}

// 安全释放资源函数
static void safe_release_resources(void)
{
    if (qlcd_state.panel_handle)
    {
        esp_lcd_panel_del(qlcd_state.panel_handle);
        qlcd_state.panel_handle = NULL;
    }

    if (qlcd_state.io_handle)
    {
        esp_lcd_panel_io_del(qlcd_state.io_handle);
        qlcd_state.io_handle = NULL;
    }

    // 修复：修正重复判断SPI_HOST的逻辑
    if (qlcd_state.spi_host != SPI3_HOST)
    {
        spi_bus_free(qlcd_state.spi_host);
        qlcd_state.spi_host = SPI3_HOST; // 重置为默认值
    }
}

// RGB565转BGR565（解决ST7789颜色格式不匹配问题）
// 正确的RGB565转BGR565函数（添加注释，便于验证）
static inline uint16_t rgb565_to_bgr565(uint16_t rgb)
{
    // 提取RGB各分量
    uint16_t r = (rgb & 0xF800) >> 11;  // 取高5位R
    uint16_t g = (rgb & 0x07E0) >> 5;   // 取中间6位G
    uint16_t b = (rgb & 0x001F);        // 取低5位B
    
    // 重组为BGR565：B(5位)<<11 + G(6位)<<5 + R(5位)
    return (b << 11) | (g << 5) | r;
}

// 修复ST7789偏移设置（ESP-IDF v5.1.6兼容，移除esp_lcd_panel_get_io_handle调用）
static esp_err_t st7789_set_offset(esp_lcd_panel_handle_t panel, uint16_t x_off, uint16_t y_off)
{
    // 直接使用全局的io_handle，无需从panel获取
    if (!qlcd_state.io_handle) return ESP_ERR_INVALID_ARG;

    // ST7789 垂直偏移指令 (0x37)
    uint8_t voffset_data = y_off & 0xFF;
    esp_err_t ret = esp_lcd_panel_io_tx_param(qlcd_state.io_handle, 0x37, &voffset_data, 1);
    if (ret != ESP_OK) return ret;

    // ST7789 水平偏移指令 (0x38)
    uint8_t hoffset_data = x_off & 0xFF;
    ret = esp_lcd_panel_io_tx_param(qlcd_state.io_handle, 0x38, &hoffset_data, 1);
    return ret;
}

// 显示刷新回调函数（适配LVGL 7.11）
static void _disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL)
    {
        lv_disp_flush_ready(drv);
        return;
    }

    // 修正坐标到有效范围（使用显式定义的LV_CLAMP）
    int x1 = LV_CLAMP(0, area->x1, qlcd_state.config.width - 1);
    int y1 = LV_CLAMP(0, area->y1, qlcd_state.config.height - 1);
    int x2 = LV_CLAMP(0, area->x2, qlcd_state.config.width - 1);
    int y2 = LV_CLAMP(0, area->y2, qlcd_state.config.height - 1);

    // 检查修正后的坐标是否有效
    if (x1 > x2 || y1 > y2)
    {
        lv_disp_flush_ready(drv);
        return;
    }

    // 核心修复：计算color_map的偏移（原area和修正后area的差值）
    int32_t x_offset = x1 - area->x1;
    int32_t y_offset = y1 - area->y1;
    int32_t line_length = area->x2 - area->x1 + 1;
    lv_color_t *corrected_color_map = color_map + (y_offset * line_length) + x_offset;

    /*// 转换颜色格式（RGB565 -> BGR565，匹配ST7789）
    uint16_t *bgr_buf = (uint16_t *)corrected_color_map;
    int pixel_count = (x2 - x1 + 1) * (y2 - y1 + 1);
    for (int i = 0; i < pixel_count; i++)
    {
        bgr_buf[i] = rgb565_to_bgr565(bgr_buf[i]);
    }*/

    // 发送数据到LCD（x2+1/y2+1是因为draw_bitmap的结束坐标是排他的）
    esp_err_t ret = esp_lcd_panel_draw_bitmap(qlcd_state.panel_handle,
                                              x1, y1, x2 + 1, y2 + 1,
                                              (uint16_t *)corrected_color_map);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Draw bitmap failed: %s", esp_err_to_name(ret));
    }

    // 通知LVGL完成刷新（LVGL 7.11兼容）
    lv_disp_flush_ready(drv);
}

/*
初始化QLCD液晶屏
*/
esp_err_t qlcd_init(const qlcd_config_t *config)
{
    if (qlcd_state.is_initialized)
    {
        ESP_LOGW(TAG, "QLCD already initialized");
        return ESP_OK;
    }

    if (config == NULL)
    {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 保存配置，设置默认值
    qlcd_state.config.width = config->width > 0 ? config->width : 320;
    qlcd_state.config.height = config->height > 0 ? config->height : 240;
    qlcd_state.config.spi_host = (config->spi_host >= SPI1_HOST && config->spi_host <= SPI3_HOST) 
                                 ? config->spi_host : SPI3_HOST;
    qlcd_state.config.mosi_pin = config->mosi_pin > 0 ? config->mosi_pin : 17;
    qlcd_state.config.clk_pin = config->clk_pin > 0 ? config->clk_pin : 18;
    qlcd_state.config.dc_pin = config->dc_pin > 0 ? config->dc_pin : 15;
    qlcd_state.config.rst_pin = config->rst_pin > 0 ? config->rst_pin : 7;
    qlcd_state.config.cs_pin = config->cs_pin > 0 ? config->cs_pin : 6;
    qlcd_state.config.freq = config->freq > 0 ? config->freq : 40000000; // 40MHz
    qlcd_state.config.draw_buf_height = config->draw_buf_height > 0 ? config->draw_buf_height : 20;

    // 验证引脚配置
    if (qlcd_state.config.mosi_pin < 0 || qlcd_state.config.clk_pin < 0 ||
        qlcd_state.config.dc_pin < 0 || qlcd_state.config.cs_pin < 0)
    {
        ESP_LOGE(TAG, "Invalid pin configuration");
        return ESP_ERR_INVALID_ARG;
    }

    // 初始化SPI总线
    ESP_LOGI(TAG, "Initialize SPI bus on host %d", qlcd_state.config.spi_host);

    const spi_bus_config_t buscfg = {
        .sclk_io_num = qlcd_state.config.clk_pin,
        .mosi_io_num = qlcd_state.config.mosi_pin,
        .miso_io_num = GPIO_NUM_NC, // 不使用MISO
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        // 修复：调整最大传输大小为屏幕单行*缓冲区高度*2（匹配LVGL缓冲区）
        .max_transfer_sz = qlcd_state.config.width * qlcd_state.config.draw_buf_height * sizeof(uint16_t),
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    esp_err_t ret = spi_bus_initialize(qlcd_state.config.spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    qlcd_state.spi_host = qlcd_state.config.spi_host;

    // 初始化面板IO
    ESP_LOGI(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = qlcd_state.config.dc_pin,
        .cs_gpio_num = qlcd_state.config.cs_pin,
        .pclk_hz = qlcd_state.config.freq,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        // 修复：ST7789默认SPI模式为3（CPOL=1, CPHA=1）
        .spi_mode = 3,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .flags = {
            .cs_high_active = 0,
        },
    };

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)qlcd_state.config.spi_host,
                                   &io_config, &qlcd_state.io_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "New panel IO failed: %s", esp_err_to_name(ret));
        goto cleanup_spi;
    }

    // 初始化ST7789面板
    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = qlcd_state.config.rst_pin,
        .color_space = ESP_LCD_COLOR_SPACE_RGB, 
        .bits_per_pixel = 16,
    };

    ret = esp_lcd_new_panel_st7789(qlcd_state.io_handle, &panel_config, &qlcd_state.panel_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "New panel failed: %s", esp_err_to_name(ret));
        goto cleanup_io;
    }

    // 初始化面板
    ret = esp_lcd_panel_reset(qlcd_state.panel_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
        goto cleanup_panel;
    }

    ret = esp_lcd_panel_init(qlcd_state.panel_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
        goto cleanup_panel;
    }

    // 修复：ESP-IDF v5.1.6 无esp_lcd_panel_set_offset，替换为兼容方案
    // 根据你的屏幕分辨率调整offset_x/offset_y，示例为240x240屏幕
    uint16_t offset_x = 0;
    uint16_t offset_y = 0;
    if (qlcd_state.config.width == 240 && qlcd_state.config.height == 240) {
        offset_x = 0;
        offset_y = 0;
    } else if (qlcd_state.config.width == 320 && qlcd_state.config.height == 240) {
        offset_x = 0;
        offset_y = 0;
    }
    
    // 方案1：尝试esp_lcd_panel_set_gap（ESP-IDF v5.1.6兼容）
    ret = esp_lcd_panel_set_gap(qlcd_state.panel_handle, offset_x, offset_y);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Set panel gap failed: %s, try manual offset", esp_err_to_name(ret));
        // 方案2：手动发送ST7789指令设置偏移（保底方案）
        ret = st7789_set_offset(qlcd_state.panel_handle, offset_x, offset_y);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Manual set offset failed: %s", esp_err_to_name(ret));
        }
    }

    // 颜色反转（根据屏幕实际需求调整）
    ret = esp_lcd_panel_invert_color(qlcd_state.panel_handle, true);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Color invert failed: %s", esp_err_to_name(ret));
    }

    // 根据屏幕方向调整设置
    ret = esp_lcd_panel_swap_xy(qlcd_state.panel_handle, true); // 显示翻转
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Swap XY failed: %s", esp_err_to_name(ret));
    }

    ret = esp_lcd_panel_mirror(qlcd_state.panel_handle, false, true); // 镜像
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Mirror failed: %s", esp_err_to_name(ret));
    }

    // 开启显示
    ret = esp_lcd_panel_disp_on_off(qlcd_state.panel_handle, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Display on failed: %s", esp_err_to_name(ret));
        goto cleanup_panel;
    }

    // 延时确保显示稳定
    vTaskDelay(pdMS_TO_TICKS(100));

    qlcd_state.is_initialized = true;
    ESP_LOGI(TAG, "QLCD initialized successfully");
    return ESP_OK;

cleanup_panel:
    if (qlcd_state.panel_handle)
    {
        esp_lcd_panel_del(qlcd_state.panel_handle);
        qlcd_state.panel_handle = NULL;
    }
cleanup_io:
    if (qlcd_state.io_handle)
    {
        esp_lcd_panel_io_del(qlcd_state.io_handle);
        qlcd_state.io_handle = NULL;
    }
cleanup_spi:
    spi_bus_free(qlcd_state.config.spi_host);
    qlcd_state.spi_host = SPI3_HOST;

    return ret;
}

/*
将LCD注册到LVGL（完全适配LVGL 7.11）
*/
esp_err_t qlcd_lvgl_register(void)
{
    if (!qlcd_state.is_initialized)
    {
        ESP_LOGE(TAG, "QLCD not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (qlcd_state.panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel handle is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    // 初始化LVGL（建议移到主函数，避免重复初始化）
    lv_init();

    // 修复：使用双缓冲区减少刷新条带/闪烁（LVGL 7.11兼容）
    size_t buf_size = qlcd_state.config.width * qlcd_state.config.draw_buf_height;
    static lv_color_t *buf1 = NULL;
    static lv_color_t *buf2 = NULL;

    // 释放已有的缓冲区
    if (buf1 != NULL) free(buf1);
    if (buf2 != NULL) free(buf2);
    buf1 = NULL;
    buf2 = NULL;

    // 分配内存 - 优先使用DMA内存，如果失败则尝试PSRAM
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
    buf2 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
    
    if (buf1 == NULL || buf2 == NULL)
    {
        ESP_LOGW(TAG, "DMA memory allocation failed, trying SPIRAM");
        if (buf1 == NULL) buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        if (buf2 == NULL) buf2 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        
        if (buf1 == NULL || buf2 == NULL)
        {
            // 降级为单缓冲区（保底）
            if (buf1 == NULL) buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DEFAULT);
            ESP_LOGE(TAG, "Failed to allocate dual LVGL buffer, use single buffer");
            if (buf1 == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate LVGL buffer");
                return ESP_ERR_NO_MEM;
            }
        }
    }

    if (buf1) ESP_LOGI(TAG, "LVGL buf1 allocated: %d bytes", buf_size * sizeof(lv_color_t));
    if (buf2) ESP_LOGI(TAG, "LVGL buf2 allocated: %d bytes", buf_size * sizeof(lv_color_t));

    // 核心适配：LVGL 7.11 使用 lv_disp_buf_t 而非 lv_disp_draw_buf_t
    static lv_disp_buf_t draw_buf;
    if (buf2) {
        lv_disp_buf_init(&draw_buf, buf1, buf2, buf_size); // 双缓冲区（LVGL 7.11）
    } else {
        lv_disp_buf_init(&draw_buf, buf1, NULL, buf_size); // 单缓冲区（LVGL 7.11）
    }

    // 初始化显示驱动（适配LVGL 7.11，移除8.x专属字段）
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = qlcd_state.config.width;
    disp_drv.ver_res = qlcd_state.config.height;
    disp_drv.flush_cb = _disp_flush;
    disp_drv.buffer = &draw_buf;

    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    if (disp == NULL)
    {
        ESP_LOGE(TAG, "Failed to register display with LVGL");
        free(buf1);
        free(buf2);
        buf1 = NULL;
        buf2 = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "QLCD registered with LVGL 7.11 successfully");
    return ESP_OK;
}

/*
LCD进入休眠模式
*/
esp_err_t qlcd_sleep(void)
{
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "LCD entering sleep mode");

    // 关闭显示
    esp_err_t ret = esp_lcd_panel_disp_on_off(qlcd_state.panel_handle, false);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to turn off display: %s", esp_err_to_name(ret));
        return ret;
    }

    // 发送睡眠命令（ST7789特定）
    uint8_t sleep_cmd = 0x10; // Sleep in command
    ret = esp_lcd_panel_io_tx_param(qlcd_state.io_handle, sleep_cmd, NULL, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Sleep command failed: %s", esp_err_to_name(ret));
    }

    // 延时确保进入睡眠状态
    vTaskDelay(pdMS_TO_TICKS(120));

    return ESP_OK;
}

/*
LCD唤醒
*/
esp_err_t qlcd_wakeup(void)
{
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "LCD waking up from sleep mode");

    // 发送唤醒命令（ST7789特定）
    uint8_t wake_cmd = 0x11; // Sleep out command
    esp_err_t ret = esp_lcd_panel_io_tx_param(qlcd_state.io_handle, wake_cmd, NULL, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Wake command failed: %s", esp_err_to_name(ret));
    }

    // 延时等待LCD稳定
    vTaskDelay(pdMS_TO_TICKS(120));

    // 修复：移除重复的panel_init调用（仅初始化一次即可）
    // 恢复显示设置（仅恢复必要的，无需重新初始化）
    esp_lcd_panel_invert_color(qlcd_state.panel_handle, true);
    esp_lcd_panel_swap_xy(qlcd_state.panel_handle, true);
    esp_lcd_panel_mirror(qlcd_state.panel_handle, false, true);

    // 开启显示
    ret = esp_lcd_panel_disp_on_off(qlcd_state.panel_handle, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to turn on display: %s", esp_err_to_name(ret));
        return ret;
    }

    // 延时确保显示稳定
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

/*
设置液晶屏颜色
*/
void qlcd_set_color(uint16_t color)
{
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL)
    {
        ESP_LOGE(TAG, "QLCD not initialized or panel handle is NULL");
        return;
    }

    // 修复：预分配缓冲区，避免逐行malloc/free（减少内存碎片）
    uint16_t *buffer = (uint16_t *)heap_caps_malloc(qlcd_state.config.width * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate color buffer");
        return;
    }

    // 转换颜色格式（RGB->BGR）
    uint16_t bgr_color = rgb565_to_bgr565(color);
    // 填充缓冲区
    for (size_t i = 0; i < qlcd_state.config.width; i++)
    {
        buffer[i] = bgr_color;
    }

    // 显示整屏颜色
    esp_err_t ret = ESP_OK;
    for (int y = 0; y < qlcd_state.config.height; y++)
    {
        ret = esp_lcd_panel_draw_bitmap(qlcd_state.panel_handle,
                                        0, y,
                                        qlcd_state.config.width, y + 1,
                                        buffer);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Draw color line failed at y=%d: %s", y, esp_err_to_name(ret));
            break;
        }
    }

    heap_caps_free(buffer);
}

/*
显示图片
*/
void qlcd_draw_picture(int x_start, int y_start, int x_end, int y_end, const unsigned char *gImage)
{
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL)
    {
        ESP_LOGE(TAG, "QLCD not initialized or panel handle is NULL");
        return;
    }

    if (gImage == NULL)
    {
        ESP_LOGE(TAG, "Image data is NULL");
        return;
    }

    // 修正坐标范围判断
    int valid_x1 = LV_CLAMP(0, x_start, qlcd_state.config.width - 1);
    int valid_y1 = LV_CLAMP(0, y_start, qlcd_state.config.height - 1);
    int valid_x2 = LV_CLAMP(0, x_end - 1, qlcd_state.config.width - 1); // 转为排他坐标
    int valid_y2 = LV_CLAMP(0, y_end - 1, qlcd_state.config.height - 1);

    if (valid_x1 > valid_x2 || valid_y1 > valid_y2)
    {
        ESP_LOGE(TAG, "Invalid picture coordinates: x[%d,%d] y[%d,%d]", x_start, x_end, y_start, y_end);
        return;
    }

    int width = valid_x2 - valid_x1 + 1;
    int height = valid_y2 - valid_y1 + 1;
    size_t pixels_byte_size = width * height * 2; // 16-bit color

    // 修复：使用DMA内存分配，转换颜色格式
    uint16_t *pixels = (uint16_t *)heap_caps_malloc(pixels_byte_size, MALLOC_CAP_DMA);
    if (pixels == NULL)
    {
        ESP_LOGE(TAG, "Memory for bitmap is not enough: %d bytes", pixels_byte_size);
        return;
    }

    // 拷贝并转换颜色格式（RGB->BGR）
    memcpy(pixels, gImage, pixels_byte_size);
    for (int i = 0; i < width * height; i++)
    {
        pixels[i] = rgb565_to_bgr565(pixels[i]);
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(qlcd_state.panel_handle,
                                              valid_x1, valid_y1,
                                              valid_x2 + 1, valid_y2 + 1,
                                              pixels);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Draw bitmap failed: %s", esp_err_to_name(ret));
    }

    heap_caps_free(pixels);
}

/*
清理资源
*/
esp_err_t qlcd_cleanup(void)
{
    if (!qlcd_state.is_initialized)
    {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Cleaning up QLCD resources");

    // 关闭显示
    if (qlcd_state.panel_handle)
    {
        esp_lcd_panel_disp_on_off(qlcd_state.panel_handle, false);
        esp_lcd_panel_del(qlcd_state.panel_handle);
        qlcd_state.panel_handle = NULL;
    }

    if (qlcd_state.io_handle)
    {
        esp_lcd_panel_io_del(qlcd_state.io_handle);
        qlcd_state.io_handle = NULL;
    }

    // 修复：修正SPI总线释放逻辑
    if (qlcd_state.spi_host != SPI3_HOST)
    {
        spi_bus_free(qlcd_state.spi_host);
        qlcd_state.spi_host = SPI3_HOST;
    }

    qlcd_state.is_initialized = false;
    memset(&qlcd_state.config, 0, sizeof(qlcd_state.config));

    ESP_LOGI(TAG, "QLCD resources cleaned up");
    return ESP_OK;
}