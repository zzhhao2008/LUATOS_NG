#include "ft6336u_driver.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "ft6336u";
static uint16_t s_usLimitX = 0;
static uint16_t s_usLimitY = 0;
static bool s_swapxy = false;
static bool s_mirror_x = false;
static bool s_mirror_y = false;

#define FT6336U_I2C_TIMEOUT_MS 1000  // 增加超时时间到1秒
#define FT6336U_DEFAULT_I2C_FREQ 100000  // 100kHz，更稳定的频率

static esp_err_t ft6336u_i2c_master_write_read(uint8_t reg_addr, uint8_t *data_rd, size_t size_rd, TickType_t timeout) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        return ESP_FAIL;
    }
    
    esp_err_t ret = ESP_OK;
    
    // 写入寄存器地址
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (FT6336U_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    
    if (size_rd > 0) {
        // 重复START条件
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (FT6336U_ADDR << 1) | I2C_MASTER_READ, true);
        
        if (size_rd > 1) {
            i2c_master_read(cmd, data_rd, size_rd - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, data_rd + size_rd - 1, I2C_MASTER_NACK);
    }
    
    i2c_master_stop(cmd);
    
    ret = i2c_master_cmd_begin(FT6336U_I2C_NUM, cmd, timeout);
    
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t ft6336u_i2c_write_reg(uint8_t reg_addr, uint8_t value, TickType_t timeout) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        return ESP_FAIL;
    }
    
    esp_err_t ret = ESP_OK;
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (FT6336U_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    
    ret = i2c_master_cmd_begin(FT6336U_I2C_NUM, cmd, timeout);
    
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t ft6336u_init(ft6336u_cfg_t* cfg) {
    // 保存配置
    s_usLimitX = cfg->x_limit;
    s_usLimitY = cfg->y_limit;
    s_swapxy = cfg->swapxy;
    s_mirror_x = cfg->mirror_x;
    s_mirror_y = cfg->mirror_y;

    // 配置RST引脚(如果提供)
    if (cfg->rst != -1) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << cfg->rst),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "RST pin config failed: %s", esp_err_to_name(ret));
            return ret;
        }
        
        gpio_set_level(cfg->rst, 0);  // 复位
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cfg->rst, 1);  // 释放复位
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 配置INT引脚(如果提供)
    if (cfg->int_pin != -1) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << cfg->int_pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "INT pin config failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // 配置I2C - 确保启用内部上拉
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = cfg->sda,
        .scl_io_num = cfg->scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,  // 启用SDA内部上拉
        .scl_pullup_en = GPIO_PULLUP_ENABLE,  // 启用SCL内部上拉
        .master.clk_speed = cfg->fre > 0 ? cfg->fre : FT6336U_DEFAULT_I2C_FREQ,
    };
    
    // 正确的I2C驱动管理顺序：先删除，再配置，再安装
    esp_err_t ret = i2c_driver_delete(FT6336U_I2C_NUM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C driver delete warning: %s", esp_err_to_name(ret));
    }
    
    ret = i2c_param_config(FT6336U_I2C_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = i2c_driver_install(FT6336U_I2C_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 增加I2C总线稳定时间
    vTaskDelay(pdMS_TO_TICKS(50));

    // 读取设备信息
    uint8_t read_buf[1];
    bool touch_detected = false;

    // 读取芯片ID - 使用更长的超时时间
    ret = ft6336u_i2c_master_write_read(0xA3, read_buf, 1, pdMS_TO_TICKS(FT6336U_I2C_TIMEOUT_MS));
    if (ret == ESP_OK) {
        touch_detected = true;
        ESP_LOGI(TAG, "Chip ID: 0x%02x", read_buf[0]);
        
        // 读取固件版本
        ret = ft6336u_i2c_master_write_read(0xA6, read_buf, 1, pdMS_TO_TICKS(FT6336U_I2C_TIMEOUT_MS));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Firmware version: 0x%02x", read_buf[0]);
        } else {
            ESP_LOGW(TAG, "Failed to read firmware version: %s", esp_err_to_name(ret));
        }

        // 读取供应商ID
        ret = ft6336u_i2c_master_write_read(0xA8, read_buf, 1, pdMS_TO_TICKS(FT6336U_I2C_TIMEOUT_MS));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "CTPM Vendor ID: 0x%02x", read_buf[0]);
        } else {
            ESP_LOGW(TAG, "Failed to read vendor ID: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to read Chip ID: %s", esp_err_to_name(ret));
        
        // 尝试I2C总线扫描来诊断问题
        ESP_LOGI(TAG, "Scanning I2C bus for devices...");
        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
            i2c_master_stop(cmd);
            
            esp_err_t scan_ret = i2c_master_cmd_begin(FT6336U_I2C_NUM, cmd, pdMS_TO_TICKS(10));
            i2c_cmd_link_delete(cmd);
            
            if (scan_ret == ESP_OK) {
                ESP_LOGI(TAG, "I2C device found at address 0x%02x", addr);
                if (addr == FT6336U_ADDR) {
                    ESP_LOGI(TAG, "Target FT6336U device found!");
                }
            }
        }
    }

    if (touch_detected) {
        ESP_LOGI(TAG, "FT6336U initialized successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "FT6336U initialization failed - device not responding");
        return ESP_FAIL;
    }
}

esp_err_t ft6336u_get_info(uint8_t *chip_id, uint8_t *firmware_version, uint8_t *vendor_id) {
    esp_err_t ret;
    uint8_t read_buf[1];
    TickType_t timeout = pdMS_TO_TICKS(FT6336U_I2C_TIMEOUT_MS);

    // 读取芯片ID
    ret = ft6336u_i2c_master_write_read(0xA3, read_buf, 1, timeout);
    if (ret == ESP_OK) {
        *chip_id = read_buf[0];
    } else {
        *chip_id = 0;
        ESP_LOGW(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
    }

    // 读取固件版本
    ret = ft6336u_i2c_master_write_read(0xA6, read_buf, 1, timeout);
    if (ret == ESP_OK) {
        *firmware_version = read_buf[0];
    } else {
        *firmware_version = 0;
        ESP_LOGW(TAG, "Failed to read firmware version: %s", esp_err_to_name(ret));
    }

    // 读取供应商ID
    ret = ft6336u_i2c_master_write_read(0xA8, read_buf, 1, timeout);
    if (ret == ESP_OK) {
        *vendor_id = read_buf[0];
    } else {
        *vendor_id = 0;
        ESP_LOGW(TAG, "Failed to read vendor ID: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

void ft6336u_read(int16_t *x, int16_t *y, int *state) {
    uint8_t touch_pnt_cnt[1];
    static int16_t last_x = 0, last_y = 0;
    TickType_t timeout = pdMS_TO_TICKS(FT6336U_I2C_TIMEOUT_MS);
    
    // 读取触摸点数量
    esp_err_t ret = ft6336u_i2c_master_write_read(0x02, touch_pnt_cnt, 1, timeout);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to read touch point count: %s", esp_err_to_name(ret));
        *x = last_x;
        *y = last_y;
        *state = 0;
        return;
    }

    // 检查触摸点数量 - 只处理单点触摸
    uint8_t touch_count = touch_pnt_cnt[0] & 0x0F;
    if (touch_count == 0) {
        *x = last_x;
        *y = last_y;
        *state = 0;
        return;
    } else if (touch_count > 1) {
        // 多点触摸，只取第一个点
        ESP_LOGD(TAG, "Multi-touch detected (%d points), using first point only", touch_count);
    }

    uint8_t data_x[2], data_y[2];
    
    // 读取X坐标
    ret = ft6336u_i2c_master_write_read(0x03, data_x, 2, timeout);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read X coordinate: %s", esp_err_to_name(ret));
        *x = last_x;
        *y = last_y;
        *state = 0;
        return;
    }

    // 读取Y坐标
    ret = ft6336u_i2c_master_write_read(0x05, data_y, 2, timeout);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read Y coordinate: %s", esp_err_to_name(ret));
        *x = last_x;
        *y = last_y;
        *state = 0;
        return;
    }

    int16_t current_x = ((data_x[0] & 0x0F) << 8) | data_x[1];
    int16_t current_y = ((data_y[0] & 0x0F) << 8) | data_y[1];

    // 应用坐标转换
    if (s_mirror_x) {
        current_x = s_usLimitX - 1 - current_x;
    }
    if (s_mirror_y) {
        current_y = s_usLimitY - 1 - current_y;
    }
    if (s_swapxy) {
        int16_t temp = current_x;
        current_x = current_y;
        current_y = temp;
    }

    // 边界检查
    if (current_x < 0) current_x = 0;
    if (current_y < 0) current_y = 0;
    if (current_x >= s_usLimitX) current_x = s_usLimitX - 1;
    if (current_y >= s_usLimitY) current_y = s_usLimitY - 1;

    // 更新最后位置
    last_x = current_x;
    last_y = current_y;

    *x = last_x;
    *y = last_y;
    *state = 1;
}