#include "qlcd.h"
#include "lvgl.h"

static const char *TAG = "QLCD";

// QLCD状态实例
static qlcd_state_t qlcd_state = {
    .is_initialized = false,
    .panel_handle = NULL,
    .io_handle = NULL,
    .spi_host = SPI3_HOST
};

// 获取QLCD状态
qlcd_state_t* qlcd_get_state(void) {
    return &qlcd_state;
}

// 安全释放资源函数
static void safe_release_resources(void) {
    if (qlcd_state.panel_handle) {
        esp_lcd_panel_del(qlcd_state.panel_handle);
        qlcd_state.panel_handle = NULL;
    }
    
    if (qlcd_state.io_handle) {
        esp_lcd_panel_io_del(qlcd_state.io_handle);
        qlcd_state.io_handle = NULL;
    }
    
    if (qlcd_state.spi_host != SPI3_HOST && qlcd_state.spi_host != SPI3_HOST) {
        // 只释放我们初始化的SPI总线
        spi_bus_free(qlcd_state.spi_host);
        qlcd_state.spi_host = SPI3_HOST; // 重置为默认值
    }
}

// 显示刷新回调函数
static void _disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL) {
        lv_disp_flush_ready(drv);
        return;
    }
    
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;
    
    // 检查坐标范围
    if (x1 >= qlcd_state.config.width || y1 >= qlcd_state.config.height || 
        x2 < 0 || y2 < 0 || x1 > x2 || y1 > y2) {
        lv_disp_flush_ready(drv);
        return;
    }
    
    // 限制坐标在有效范围内
    x1 = (x1 < 0) ? 0 : x1;
    y1 = (y1 < 0) ? 0 : y1;
    x2 = (x2 >= qlcd_state.config.width) ? qlcd_state.config.width - 1 : x2;
    y2 = (y2 >= qlcd_state.config.height) ? qlcd_state.config.height - 1 : y2;
    
    // 发送数据到LCD
    esp_err_t ret = esp_lcd_panel_draw_bitmap(qlcd_state.panel_handle, 
                                             x1, y1, x2 + 1, y2 + 1, 
                                             (uint16_t *)color_map);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Draw bitmap failed: %s", esp_err_to_name(ret));
    }
    
    // 通知LVGL完成刷新
    lv_disp_flush_ready(drv);
}

/*
初始化QLCD液晶屏
*/
esp_err_t qlcd_init(const qlcd_config_t *config) {
    if (qlcd_state.is_initialized) {
        ESP_LOGW(TAG, "QLCD already initialized");
        return ESP_OK;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 保存配置，设置默认值
    qlcd_state.config.width = config->width > 0 ? config->width : 320;
    qlcd_state.config.height = config->height > 0 ? config->height : 240;
    qlcd_state.config.spi_host = config->spi_host > 0 ? config->spi_host : SPI3_HOST;
    qlcd_state.config.mosi_pin = config->mosi_pin > 0 ? config->mosi_pin : 17;
    qlcd_state.config.clk_pin = config->clk_pin > 0 ? config->clk_pin : 18;
    qlcd_state.config.dc_pin = config->dc_pin > 0 ? config->dc_pin : 15;
    qlcd_state.config.rst_pin = config->rst_pin > 0 ? config->rst_pin : 7;
    qlcd_state.config.cs_pin = config->cs_pin > 0 ? config->cs_pin : 6;
    qlcd_state.config.freq = config->freq > 0 ? config->freq : 40000000; // 40MHz
    qlcd_state.config.draw_buf_height = config->draw_buf_height > 0 ? config->draw_buf_height : 20;
    
    // 验证引脚配置
    if (qlcd_state.config.mosi_pin < 0 || qlcd_state.config.clk_pin < 0 || 
        qlcd_state.config.dc_pin < 0 || qlcd_state.config.cs_pin < 0) {
        ESP_LOGE(TAG, "Invalid pin configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化SPI总线
    ESP_LOGI(TAG, "Initialize SPI bus on host %d", qlcd_state.config.spi_host);
    
    // 检查SPI主机是否有效
    if (qlcd_state.config.spi_host < SPI1_HOST || qlcd_state.config.spi_host > SPI3_HOST) {
        ESP_LOGE(TAG, "Invalid SPI host: %d", qlcd_state.config.spi_host);
        return ESP_ERR_INVALID_ARG;
    }
    
    const spi_bus_config_t buscfg = {
        .sclk_io_num = qlcd_state.config.clk_pin,
        .mosi_io_num = qlcd_state.config.mosi_pin,
        .miso_io_num = GPIO_NUM_NC,  // 不使用MISO
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = qlcd_state.config.width * 80 * sizeof(uint16_t), // 减少最大传输大小
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    
    esp_err_t ret = spi_bus_initialize(qlcd_state.config.spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
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
        .spi_mode = 2,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .flags = {
            .cs_high_active = 0,
        },
    };
    
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)qlcd_state.config.spi_host, 
                                   &io_config, &qlcd_state.io_handle);
    if (ret != ESP_OK) {
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
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New panel failed: %s", esp_err_to_name(ret));
        goto cleanup_io;
    }
    
    // 初始化面板
    ret = esp_lcd_panel_reset(qlcd_state.panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
        goto cleanup_panel;
    }
    
    ret = esp_lcd_panel_init(qlcd_state.panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
        goto cleanup_panel;
    }
    
    // 颜色反转
    ret = esp_lcd_panel_invert_color(qlcd_state.panel_handle, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Color invert failed: %s", esp_err_to_name(ret));
    }
    
    // 根据屏幕方向调整设置 - 与参考配置保持一致
    ret = esp_lcd_panel_swap_xy(qlcd_state.panel_handle, true);       // 显示翻转
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Swap XY failed: %s", esp_err_to_name(ret));
    }
    
    ret = esp_lcd_panel_mirror(qlcd_state.panel_handle, false, true); // 镜像
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Mirror failed: %s", esp_err_to_name(ret));
    }
    
    // 开启显示
    ret = esp_lcd_panel_disp_on_off(qlcd_state.panel_handle, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display on failed: %s", esp_err_to_name(ret));
        goto cleanup_panel;
    }
    
    // 延时确保显示稳定
    vTaskDelay(pdMS_TO_TICKS(100));
    
    qlcd_state.is_initialized = true;
    ESP_LOGI(TAG, "QLCD initialized successfully");
    return ESP_OK;

cleanup_panel:
    if (qlcd_state.panel_handle) {
        esp_lcd_panel_del(qlcd_state.panel_handle);
        qlcd_state.panel_handle = NULL;
    }
cleanup_io:
    if (qlcd_state.io_handle) {
        esp_lcd_panel_io_del(qlcd_state.io_handle);
        qlcd_state.io_handle = NULL;
    }
cleanup_spi:
    spi_bus_free(qlcd_state.config.spi_host);
    qlcd_state.spi_host = SPI3_HOST;
    
    return ret;
}

/*
将LCD注册到LVGL
*/
esp_err_t qlcd_lvgl_register(void) {
    if (!qlcd_state.is_initialized) {
        ESP_LOGE(TAG, "QLCD not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (qlcd_state.panel_handle == NULL) {
        ESP_LOGE(TAG, "Panel handle is NULL");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 初始化LVGL
    lv_init();
    
    // 创建显示缓冲区
    size_t buf_size = qlcd_state.config.width * qlcd_state.config.draw_buf_height;
    static lv_color_t *buf1 = NULL;
    
    // 释放已有的缓冲区
    if (buf1 != NULL) {
        free(buf1);
        buf1 = NULL;
    }
    
    // 分配内存 - 优先使用DMA内存，如果失败则尝试PSRAM
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (buf1 == NULL) {
        ESP_LOGW(TAG, "DMA memory allocation failed, trying SPIRAM");
        buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        if (buf1 == NULL) {
            ESP_LOGE(TAG, "Failed to allocate LVGL buffer");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "LVGL buffer allocated in SPIRAM: %d bytes", buf_size * sizeof(lv_color_t));
    } else {
        ESP_LOGI(TAG, "LVGL buffer allocated in DMA memory: %d bytes", buf_size * sizeof(lv_color_t));
    }
    
    // 关联缓冲区到LVGL
    static lv_disp_buf_t draw_buf;
    lv_disp_buf_init(&draw_buf, buf1, NULL, buf_size);
    
    // 初始化显示驱动
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = qlcd_state.config.width;
    disp_drv.ver_res = qlcd_state.config.height;
    disp_drv.flush_cb = _disp_flush;
    disp_drv.buffer = &draw_buf;
    
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to register display with LVGL");
        free(buf1);
        buf1 = NULL;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "QLCD registered with LVGL successfully");
    return ESP_OK;
}

/*
LCD进入休眠模式
*/
esp_err_t qlcd_sleep(void) {
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "LCD entering sleep mode");
    
    // 关闭显示
    esp_err_t ret = esp_lcd_panel_disp_on_off(qlcd_state.panel_handle, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn off display: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 发送睡眠命令（ST7789特定）
    uint8_t sleep_cmd = 0x10; // Sleep in command
    ret = esp_lcd_panel_io_tx_param(qlcd_state.io_handle, sleep_cmd, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Sleep command failed: %s", esp_err_to_name(ret));
    }
    
    // 延时确保进入睡眠状态
    vTaskDelay(pdMS_TO_TICKS(120));
    
    return ESP_OK;
}

/*
LCD唤醒
*/
esp_err_t qlcd_wakeup(void) {
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "LCD waking up from sleep mode");
    
    // 发送唤醒命令（ST7789特定）
    uint8_t wake_cmd = 0x11; // Sleep out command
    esp_err_t ret = esp_lcd_panel_io_tx_param(qlcd_state.io_handle, wake_cmd, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Wake command failed: %s", esp_err_to_name(ret));
    }
    
    // 延时等待LCD稳定
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // 重新初始化显示参数
    ret = esp_lcd_panel_init(qlcd_state.panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel re-init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 恢复显示设置
    esp_lcd_panel_invert_color(qlcd_state.panel_handle, true);
    esp_lcd_panel_swap_xy(qlcd_state.panel_handle, true);
    esp_lcd_panel_mirror(qlcd_state.panel_handle, false, true);
    
    // 开启显示
    ret = esp_lcd_panel_disp_on_off(qlcd_state.panel_handle, true);
    if (ret != ESP_OK) {
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
void qlcd_set_color(uint16_t color) {
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL) {
        ESP_LOGE(TAG, "QLCD not initialized or panel handle is NULL");
        return;
    }
    
    // 分配单行缓冲区
    uint16_t *buffer = (uint16_t *)malloc(qlcd_state.config.width * sizeof(uint16_t));
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate color buffer");
        return;
    }
    
    // 填充缓冲区
    for (size_t i = 0; i < qlcd_state.config.width; i++) {
        buffer[i] = color;
    }
    
    // 显示整屏颜色
    esp_err_t ret = ESP_OK;
    for (int y = 0; y < qlcd_state.config.height; y++) {
        ret = esp_lcd_panel_draw_bitmap(qlcd_state.panel_handle, 
                                       0, y, 
                                       qlcd_state.config.width, y + 1, 
                                       buffer);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Draw color line failed at y=%d: %s", y, esp_err_to_name(ret));
            break;
        }
    }
    
    free(buffer);
}

/*
显示图片
*/
void qlcd_draw_picture(int x_start, int y_start, int x_end, int y_end, const unsigned char *gImage) {
    if (!qlcd_state.is_initialized || qlcd_state.panel_handle == NULL) {
        ESP_LOGE(TAG, "QLCD not initialized or panel handle is NULL");
        return;
    }
    
    if (gImage == NULL) {
        ESP_LOGE(TAG, "Image data is NULL");
        return;
    }
    
    if (x_start < 0 || y_start < 0 || x_end > qlcd_state.config.width || y_end > qlcd_state.config.height || 
        x_start >= x_end || y_start >= y_end) {
        ESP_LOGE(TAG, "Invalid picture coordinates: x[%d,%d] y[%d,%d]", x_start, x_end, y_start, y_end);
        return;
    }
    
    int width = x_end - x_start;
    int height = y_end - y_start;
    size_t pixels_byte_size = width * height * 2; // 16-bit color
    
    // 分配内存
    uint16_t *pixels = (uint16_t *)malloc(pixels_byte_size);
    if (pixels == NULL) {
        ESP_LOGE(TAG, "Memory for bitmap is not enough: %d bytes", pixels_byte_size);
        return;
    }
    
    memcpy(pixels, gImage, pixels_byte_size);
    
    esp_err_t ret = esp_lcd_panel_draw_bitmap(qlcd_state.panel_handle, 
                                             x_start, y_start, 
                                             x_end, y_end, 
                                             pixels);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Draw bitmap failed: %s", esp_err_to_name(ret));
    }
    
    free(pixels);
}

/*
清理资源
*/
esp_err_t qlcd_cleanup(void) {
    if (!qlcd_state.is_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Cleaning up QLCD resources");
    
    // 关闭显示
    if (qlcd_state.panel_handle) {
        esp_lcd_panel_disp_on_off(qlcd_state.panel_handle, false);
        esp_lcd_panel_del(qlcd_state.panel_handle);
        qlcd_state.panel_handle = NULL;
    }
    
    if (qlcd_state.io_handle) {
        esp_lcd_panel_io_del(qlcd_state.io_handle);
        qlcd_state.io_handle = NULL;
    }
    
    if (qlcd_state.spi_host != SPI3_HOST && qlcd_state.spi_host != SPI3_HOST) {
        spi_bus_free(qlcd_state.spi_host);
        qlcd_state.spi_host = SPI3_HOST;
    }
    
    qlcd_state.is_initialized = false;
    memset(&qlcd_state.config, 0, sizeof(qlcd_state.config));
    
    ESP_LOGI(TAG, "QLCD resources cleaned up");
    return ESP_OK;
}