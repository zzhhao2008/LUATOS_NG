#include "qlcd.h"
#include "lvgl.h"


static const char *TAG = "QLCD";

// 定义液晶屏句柄
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static lv_disp_t *disp = NULL;
static bool is_initialized = false;

// 配置结构
typedef struct {
    int width;
    int height;
    int spi_host;
    int mosi_pin;
    int clk_pin;
    int dc_pin;
    int rst_pin;
    int cs_pin;
    int bl_pin;
    int freq;
    int draw_buf_height;
} qlcd_config_t;

static qlcd_config_t lcd_config;

// 显示刷新回调函数
static void _disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    // 发送数据到LCD
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, (uint16_t *)color_map);

    // 通知LVGL完成刷新
    lv_disp_flush_ready(drv);
}

/*
初始化QLCD液晶屏
*/
esp_err_t qlcd_init(int width, int height, int spi_host, int mosi_pin, int clk_pin, int dc_pin, int rst_pin, int cs_pin, int bl_pin, int freq, int draw_buf_height) {
    // 保存配置
    lcd_config.width = width;
    lcd_config.height = height;
    lcd_config.spi_host = spi_host;
    lcd_config.mosi_pin = mosi_pin;
    lcd_config.clk_pin = clk_pin;
    lcd_config.dc_pin = dc_pin;
    lcd_config.rst_pin = rst_pin;
    lcd_config.cs_pin = cs_pin;
    lcd_config.bl_pin = bl_pin;
    lcd_config.freq = freq;
    lcd_config.draw_buf_height = draw_buf_height;

    // 初始化SPI总线
    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = lcd_config.clk_pin,
        .mosi_io_num = lcd_config.mosi_pin,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = lcd_config.width * lcd_config.height * sizeof(uint16_t),
    };
    esp_err_t ret = spi_bus_initialize(lcd_config.spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化面板IO
    ESP_LOGI(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = lcd_config.dc_pin,
        .cs_gpio_num = lcd_config.cs_pin,
        .pclk_hz = lcd_config.freq,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 2,
        .trans_queue_depth = 10,
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)lcd_config.spi_host, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New panel IO failed: %s", esp_err_to_name(ret));
        spi_bus_free(lcd_config.spi_host);
        return ret;
    }

    // 初始化ST7789面板
    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = lcd_config.rst_pin,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New panel failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(io_handle);
        spi_bus_free(lcd_config.spi_host);
        return ret;
    }

    // 初始化面板
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, true);  // 颜色反转
    esp_lcd_panel_swap_xy(panel_handle, true);       // 显示翻转
    esp_lcd_panel_mirror(panel_handle, false, true); // 镜像

    // 开启显示
    esp_lcd_panel_disp_on_off(panel_handle, true);
    
    // 控制背光
    if (lcd_config.bl_pin >= 0) {
        gpio_pad_select_gpio(lcd_config.bl_pin);
        gpio_set_direction(lcd_config.bl_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(lcd_config.bl_pin, 1); // 开启背光
    }

    is_initialized = true;
    ESP_LOGI(TAG, "QLCD initialized successfully");
    return ESP_OK;
}

/*
将LCD注册到LVGL 7.11
*/
esp_err_t qlcd_lvgl_register(void) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "QLCD not initialized");
        return ESP_FAIL;
    }

    // 初始化LVGL
    lv_init();

    // 创建显示缓冲区
    static lv_color_t *buf1 = NULL;
    if (buf1 == NULL) {
        buf1 = (lv_color_t *)heap_caps_malloc(
            lcd_config.width * lcd_config.draw_buf_height * sizeof(lv_color_t),
            MALLOC_CAP_DMA);
        if (buf1 == NULL) {
            ESP_LOGE(TAG, "Failed to allocate LVGL buffer");
            return ESP_ERR_NO_MEM;
        }
    }

    // 关联缓冲区到LVGL
    lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, lcd_config.width * lcd_config.draw_buf_height);

    // 初始化显示驱动
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = lcd_config.width;
    disp_drv.ver_res = lcd_config.height;
    disp_drv.flush_cb = _disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "QLCD registered with LVGL successfully");
    return ESP_OK;
}

/*
设置液晶屏颜色
*/
void qlcd_set_color(uint16_t color) {
    if (!is_initialized) return;

    // 分配内存 这里分配了液晶屏一行数据需要的大小
    uint16_t *buffer = (uint16_t *)heap_caps_malloc(lcd_config.width * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);

    if (NULL == buffer) {
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
    } else {
        for (size_t i = 0; i < lcd_config.width; i++) { // 给缓存中放入颜色数据
            buffer[i] = color;
        }
        for (int y = 0; y < lcd_config.height; y++) { // 显示整屏颜色
            esp_lcd_panel_draw_bitmap(panel_handle, 0, y, lcd_config.width, y + 1, buffer);
        }
        heap_caps_free(buffer); // 释放内存
    }
}

/*
显示图片
*/
void qlcd_draw_pictrue(int x_start, int y_start, int x_end, int y_end, const unsigned char *gImage) {
    if (!is_initialized) return;

    // 分配内存 分配了需要的字节大小 且指定在外部SPIRAM中分配
    size_t pixels_byte_size = (x_end - x_start) * (y_end - y_start) * 2;
    uint16_t *pixels = (uint16_t *)heap_caps_malloc(pixels_byte_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (NULL == pixels) {
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
        return;
    }
    memcpy(pixels, gImage, pixels_byte_size);                                                    // 把图片数据拷贝到内存
    esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, (uint16_t *)pixels); // 显示整张图片数据
    heap_caps_free(pixels);                                                                      // 释放内存
}