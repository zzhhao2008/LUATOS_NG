#include "es8311_driver.h"
#include "driver/i2s_std.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static const char *TAG = "es8311";

// 默认GPIO配置
#define ES8311_DEFAULT_SCL      GPIO_NUM_9
#define ES8311_DEFAULT_SDA      GPIO_NUM_10
#define ES8311_DEFAULT_MCLK     GPIO_NUM_4
#define ES8311_DEFAULT_SCLK     GPIO_NUM_5
#define ES8311_DEFAULT_ASDOUT   GPIO_NUM_46
#define ES8311_DEFAULT_LRCK     GPIO_NUM_12
#define ES8311_DEFAULT_DSIN     GPIO_NUM_45

#define ES8311_DEFAULT_FREQ    100000  // 100kHz
#define ES8311_I2C_TIMEOUT_MS 1000

// I2S配置
#define ES8311_I2S_PORT        I2S_NUM_0
#define ES8311_I2S_BCLK_IO     ES8311_DEFAULT_SCLK
#define ES8311_I2S_WS_IO       ES8311_DEFAULT_LRCK
#define ES8311_I2S_DO_IO       ES8311_DEFAULT_ASDOUT
#define ES8311_I2S_DI_IO       ES8311_DEFAULT_DSIN
#define ES8311_DMA_BUF_COUNT   8
#define ES8311_DMA_BUF_LEN     512

// 全局配置和状态
static es8311_cfg_t g_es8311_cfg = {0};
static bool g_es8311_initialized = false;
static es8311_audio_mode_t g_current_mode = ES8311_MODE_DAC;
static bool g_i2s_initialized = false;
static i2s_chan_handle_t g_tx_handle = NULL;
static i2s_chan_handle_t g_rx_handle = NULL;
static bool g_i2c_driver_installed = false;  // I2C驱动安装状态

// 采样率分频系数表
typedef struct {
    uint32_t rate;
    uint8_t div;
} es8311_rate_div_t;

static const es8311_rate_div_t es8311_rate_divs[] = {
    {8000, 0x06},
    {16000, 0x03},
    {32000, 0x01},
    {44100, 0x00},
    {48000, 0x00},
};

// 检查I2C驱动是否已安装
static bool i2c_driver_installed_check(i2c_port_t port) {
    // 在IDF 5.x中，没有直接的API来检查，我们尝试安装，如果返回ESP_ERR_INVALID_STATE则说明已安装
    return false; // 始终尝试安装，让ESP-IDF处理重复安装
}

// I2C写寄存器

// I2C写寄存器
static esp_err_t es8311_i2c_write_reg(uint8_t reg_addr, uint8_t value) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[2] = {reg_addr, value};
    esp_err_t ret = i2c_master_write_to_device(g_es8311_cfg.i2c_num, ES8311_I2C_ADDR, data, sizeof(data), pdMS_TO_TICKS(ES8311_I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write reg 0x%02X failed: %s", reg_addr, esp_err_to_name(ret));
    }

    return ret;
}

// I2C读寄存器
static esp_err_t es8311_i2c_read_reg(uint8_t reg_addr, uint8_t *value) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_write_read_device(g_es8311_cfg.i2c_num, ES8311_I2C_ADDR, &reg_addr, 1, value, 1, pdMS_TO_TICKS(ES8311_I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read reg 0x%02X failed: %s", reg_addr, esp_err_to_name(ret));
    }

    return ret;
}

// 获取采样率分频系数
static uint8_t es8311_get_rate_div(uint32_t sample_rate) {
    for (size_t i = 0; i < sizeof(es8311_rate_divs) / sizeof(es8311_rate_div_t); i++) {
        if (es8311_rate_divs[i].rate == sample_rate) {
            return es8311_rate_divs[i].div;
        }
    }
    return 0x00; // 默认使用48k的配置
}

// 初始化I2S
static esp_err_t es8311_i2s_init(es8311_audio_mode_t mode, uint32_t sample_rate) {
    // 如果已初始化，先清理
    if (g_tx_handle) {
        i2s_channel_disable(g_tx_handle);
        i2s_del_channel(g_tx_handle);
        g_tx_handle = NULL;
    }
    if (g_rx_handle) {
        i2s_channel_disable(g_rx_handle);
        i2s_del_channel(g_rx_handle);
        g_rx_handle = NULL;
    }

    // 创建I2S标准模式配置
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = ES8311_I2S_BCLK_IO,
            .ws = ES8311_I2S_WS_IO,
            .dout = (mode == ES8311_MODE_ADC) ? GPIO_NUM_NC : ES8311_I2S_DO_IO,
            .din = (mode == ES8311_MODE_DAC) ? GPIO_NUM_NC : ES8311_I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // 创建I2S通道配置
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    // 根据模式创建TX和/或RX通道
    esp_err_t ret;
    if (mode == ES8311_MODE_DAC) {
        // 仅TX
        ret = i2s_new_channel(&chan_cfg, &g_tx_handle, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2S TX channel: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = i2s_channel_init_std_mode(g_tx_handle, &std_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init I2S TX channel: %s", esp_err_to_name(ret));
            i2s_del_channel(g_tx_handle);
            g_tx_handle = NULL;
            return ret;
        }
        ret = i2s_channel_enable(g_tx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable I2S TX channel: %s", esp_err_to_name(ret));
            i2s_del_channel(g_tx_handle);
            g_tx_handle = NULL;
            return ret;
        }
        g_i2s_initialized = true;
        ESP_LOGI(TAG, "I2S TX channel initialized");
    } else if (mode == ES8311_MODE_ADC) {
        // 仅RX
        ret = i2s_new_channel(&chan_cfg, NULL, &g_rx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2S RX channel: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = i2s_channel_init_std_mode(g_rx_handle, &std_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init I2S RX channel: %s", esp_err_to_name(ret));
            i2s_del_channel(g_rx_handle);
            g_rx_handle = NULL;
            return ret;
        }
        ret = i2s_channel_enable(g_rx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable I2S RX channel: %s", esp_err_to_name(ret));
            i2s_del_channel(g_rx_handle);
            g_rx_handle = NULL;
            return ret;
        }
        g_i2s_initialized = true;
        ESP_LOGI(TAG, "I2S RX channel initialized");
    } else if (mode == ES8311_MODE_BOTH) {
        // TX和RX
        ret = i2s_new_channel(&chan_cfg, &g_tx_handle, &g_rx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2S TX/RX channels: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = i2s_channel_init_std_mode(g_tx_handle, &std_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init I2S TX channel: %s", esp_err_to_name(ret));
            i2s_del_channel(g_tx_handle);
            i2s_del_channel(g_rx_handle);
            g_tx_handle = NULL;
            g_rx_handle = NULL;
            return ret;
        }
        ret = i2s_channel_init_std_mode(g_rx_handle, &std_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init I2S RX channel: %s", esp_err_to_name(ret));
            i2s_del_channel(g_tx_handle);
            i2s_del_channel(g_rx_handle);
            g_tx_handle = NULL;
            g_rx_handle = NULL;
            return ret;
        }
        ret = i2s_channel_enable(g_tx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable I2S TX channel: %s", esp_err_to_name(ret));
            i2s_del_channel(g_tx_handle);
            i2s_del_channel(g_rx_handle);
            g_tx_handle = NULL;
            g_rx_handle = NULL;
            return ret;
        }
        ret = i2s_channel_enable(g_rx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable I2S RX channel: %s", esp_err_to_name(ret));
            i2s_channel_disable(g_tx_handle);
            i2s_del_channel(g_tx_handle);
            i2s_del_channel(g_rx_handle);
            g_tx_handle = NULL;
            g_rx_handle = NULL;
            return ret;
        }
        g_i2s_initialized = true;
        ESP_LOGI(TAG, "I2S TX/RX channels initialized");
    }

    return ESP_OK;
}

esp_err_t es8311_init(es8311_cfg_t *cfg) {
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 保存配置
    g_es8311_cfg = *cfg;

    // 设置默认值
    if (cfg->scl == 0) cfg->scl = ES8311_DEFAULT_SCL;
    if (cfg->sda == 0) cfg->sda = ES8311_DEFAULT_SDA;
    if (cfg->mclk == 0) cfg->mclk = ES8311_DEFAULT_MCLK;
    if (cfg->sclk == 0) cfg->sclk = ES8311_DEFAULT_SCLK;
    if (cfg->asdout == 0) cfg->asdout = ES8311_DEFAULT_ASDOUT;
    if (cfg->lrck == 0) cfg->lrck = ES8311_DEFAULT_LRCK;
    if (cfg->dsin == 0) cfg->dsin = ES8311_DEFAULT_DSIN;
    if (cfg->fre == 0) cfg->fre = ES8311_DEFAULT_FREQ;

    ESP_LOGI(TAG, "Initializing ES8311...");
    ESP_LOGI(TAG, "  I2C: SCL=%d, SDA=%d, Port=%d, Freq=%lu", cfg->scl, cfg->sda, cfg->i2c_num, cfg->fre);
    ESP_LOGI(TAG, "  MCLK=%d, SCLK=%d, ASDOUT=%d, LRCK=%d, DSIN=%d",
             cfg->mclk, cfg->sclk, cfg->asdout, cfg->lrck, cfg->dsin);

    // 检查I2C驱动是否已安装（支持总线共享）
    if (!g_i2c_driver_installed) {
        i2c_config_t i2c_conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = cfg->sda,
            .scl_io_num = cfg->scl,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = cfg->fre,
        };

        esp_err_t ret = i2c_param_config(cfg->i2c_num, &i2c_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = i2c_driver_install(cfg->i2c_num, I2C_MODE_MASTER, 0, 0, 0);
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(TAG, "I2C driver already installed, sharing bus");
            g_i2c_driver_installed = false;  // 标记为共享总线
        } else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
            return ret;
        } else {
            g_i2c_driver_installed = true;
        }
    }

    // 检查芯片ID
    uint8_t chip_id, version;
    esp_err_t ret = es8311_get_info(&chip_id, &version);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID");
        return ret;
    }

    if (chip_id != 0x83) {
        ESP_LOGE(TAG, "Invalid chip ID: 0x%02X (expected 0x83)", chip_id);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "ES8311 chip detected, version: 0x%02X", version);

    // 配置MCLK引脚（如果需要）
    if (cfg->mclk != GPIO_NUM_NC) {
        gpio_set_direction(cfg->mclk, GPIO_MODE_OUTPUT);
        gpio_set_level(cfg->mclk, 0);
        ESP_LOGI(TAG, "MCLK GPIO configured");
    }

    // 复位芯片
    es8311_i2c_write_reg(ES8311_RESET_REG00, 0x80);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 初始化时钟管理
    es8311_i2c_write_reg(ES8311_CLK_MANAGER_REG01, 0x3F);
    es8311_i2c_write_reg(ES8311_CLK_MANAGER_REG02, 0x00);

    // 配置系统控制
    es8311_i2c_write_reg(ES8311_SYSTEM_REG0B, 0x00);
    es8311_i2c_write_reg(ES8311_SYSTEM_REG0C, 0x00);
    es8311_i2c_write_reg(ES8311_SYSTEM_REG10, 0x03); // 3.3V供电
    es8311_i2c_write_reg(ES8311_SYSTEM_REG11, 0x7F);

    // 配置SDP in/out
    uint8_t sdpin = es8311_i2c_read_reg(ES8311_SDPIN_REG09, &sdpin) == ESP_OK ? sdpin : 0;
    es8311_i2c_write_reg(ES8311_SDPIN_REG09, sdpin & 0xBF); // I2S格式

    uint8_t sdpout = es8311_i2c_read_reg(ES8311_SDPOUT_REG0A, &sdpout) == ESP_OK ? sdpout : 0;
    es8311_i2c_write_reg(ES8311_SDPOUT_REG0A, sdpout & 0xBF);

    // 配置DAC
    es8311_i2c_write_reg(ES8311_SYSTEM_REG12, 0x28);
    es8311_i2c_write_reg(ES8311_SYSTEM_REG13, 0x00);

    // 配置ADC
    es8311_i2c_write_reg(ES8311_SYSTEM_REG14, 0x10); // Mic1p-Mic1n

    // 设置为正常电源模式
    es8311_i2c_write_reg(ES8311_SYSTEM_REG0E, 0x02);

    // 配置DAC ramp
    es8311_i2c_write_reg(ES8311_DAC_REG37, 0x08);

    // 配置ADC HPF
    es8311_i2c_write_reg(ES8311_ADC_REG1B, 0x0A);
    es8311_i2c_write_reg(ES8311_ADC_REG1C, 0x6A);

    g_es8311_initialized = true;
    g_current_mode = ES8311_MODE_DAC;

    ESP_LOGI(TAG, "ES8311 initialized successfully");

    return ESP_OK;
}

esp_err_t es8311_deinit(void) {
    if (!g_es8311_initialized) {
        return ESP_OK;
    }

    // 停止I2S
    if (g_tx_handle) {
        i2s_channel_disable(g_tx_handle);
        i2s_del_channel(g_tx_handle);
        g_tx_handle = NULL;
    }
    if (g_rx_handle) {
        i2s_channel_disable(g_rx_handle);
        i2s_del_channel(g_rx_handle);
        g_rx_handle = NULL;
    }
    g_i2s_initialized = false;

    // 静音
    es8311_set_mute(1);

    // 关闭电源
    es8311_i2c_write_reg(ES8311_SYSTEM_REG0E, 0xFF);

    // 删除I2C驱动（如果是自己安装的）
    if (g_i2c_driver_installed) {
        i2c_driver_delete(g_es8311_cfg.i2c_num);
        g_i2c_driver_installed = false;
    }

    g_es8311_initialized = false;
    ESP_LOGI(TAG, "ES8311 deinitialized");

    return ESP_OK;
}

esp_err_t es8311_set_sample_rate(uint32_t sample_rate) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t div = es8311_get_rate_div(sample_rate);
    uint8_t reg = 0x07 & 0x07; // 保留低3位
    reg |= (div << 3);

    esp_err_t ret = es8311_i2c_write_reg(ES8311_CLK_MANAGER_REG02, reg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Set sample rate: %lu Hz", sample_rate);
    }

    return ret;
}

esp_err_t es8311_set_mode(es8311_audio_mode_t mode) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    g_current_mode = mode;
    ESP_LOGI(TAG, "Audio mode set to: %d", mode);

    return ESP_OK;
}

esp_err_t es8311_set_volume(uint8_t volume) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (volume > 100) {
        volume = 100;
    }

    // 转换为寄存器值 (0-255)
    uint8_t reg_value = (uint8_t)(volume * 255 / 100);

    esp_err_t ret = es8311_i2c_write_reg(ES8311_DAC_REG32, reg_value);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Set volume: %d%%", volume);
    }

    return ret;
}

esp_err_t es8311_get_volume(uint8_t *volume) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (volume == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_value;
    esp_err_t ret = es8311_i2c_read_reg(ES8311_DAC_REG32, &reg_value);
    if (ret == ESP_OK) {
        // 转换回百分比
        *volume = (uint8_t)(reg_value * 100 / 255);
    }

    return ret;
}

esp_err_t es8311_set_mute(uint8_t enable) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg;
    esp_err_t ret = es8311_i2c_read_reg(ES8311_DAC_REG31, &reg);
    if (ret != ESP_OK) {
        return ret;
    }

    if (enable) {
        reg |= 0x20; // 设置静音位
    } else {
        reg &= ~0x20; // 清除静音位
    }

    ret = es8311_i2c_write_reg(ES8311_DAC_REG31, reg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Set mute: %s", enable ? "ON" : "OFF");
    }

    return ret;
}

esp_err_t es8311_get_mute(uint8_t *enabled) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg;
    esp_err_t ret = es8311_i2c_read_reg(ES8311_DAC_REG31, &reg);
    if (ret == ESP_OK) {
        *enabled = (reg & 0x20) >> 5;
    }

    return ret;
}

esp_err_t es8311_get_info(uint8_t *chip_id, uint8_t *version) {
    if (chip_id == NULL || version == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    ret = es8311_i2c_read_reg(ES8311_CHD1_REGFD, chip_id);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = es8311_i2c_read_reg(ES8311_CHVER_REGFF, version);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t es8311_start(es8311_audio_mode_t mode) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 初始化I2S
    esp_err_t ret = es8311_i2s_init(mode, 44100); // 默认44100Hz
    if (ret != ESP_OK) {
        return ret;
    }

    // 取消静音
    es8311_set_mute(0);

    // 根据模式使能对应的通道
    if (mode == ES8311_MODE_DAC) {
        es8311_i2c_write_reg(ES8311_SYSTEM_REG12, 0x28); // 使能DAC
        ESP_LOGI(TAG, "ES8311 DAC started");
    } else if (mode == ES8311_MODE_ADC) {
        es8311_i2c_write_reg(ES8311_SYSTEM_REG14, 0x10); // 使能ADC
        ESP_LOGI(TAG, "ES8311 ADC started");
    } else if (mode == ES8311_MODE_BOTH) {
        es8311_i2c_write_reg(ES8311_SYSTEM_REG12, 0x28); // 使能DAC
        es8311_i2c_write_reg(ES8311_SYSTEM_REG14, 0x10); // 使能ADC
        ESP_LOGI(TAG, "ES8311 DAC and ADC started");
    }

    return ESP_OK;
}

esp_err_t es8311_stop(void) {
    if (!g_es8311_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 停止I2S
    if (g_i2s_initialized) {
        if (g_tx_handle) {
            i2s_channel_disable(g_tx_handle);
        }
        if (g_rx_handle) {
            i2s_channel_disable(g_rx_handle);
        }
    }

    // 静音
    es8311_set_mute(1);

    ESP_LOGI(TAG, "ES8311 stopped");
    return ESP_OK;
}

esp_err_t es8311_write_audio(uint8_t *data, size_t len) {
    if (!g_es8311_initialized || !g_i2s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_current_mode == ES8311_MODE_ADC || g_tx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_written = 0;
    size_t remaining = len;
    const uint8_t *ptr = data;

    // 分块写入数据
    while (remaining > 0) {
        size_t write_len = (remaining > ES8311_DMA_BUF_LEN) ? ES8311_DMA_BUF_LEN : remaining;
        esp_err_t ret = i2s_channel_write(g_tx_handle, ptr, write_len, &bytes_written, pdMS_TO_TICKS(1000));

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
            return ret;
        }

        ptr += bytes_written;
        remaining -= bytes_written;
    }

    return ESP_OK;
}

esp_err_t es8311_read_audio(uint8_t *data, size_t len, size_t *actual_len) {
    if (!g_es8311_initialized || !g_i2s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || len == 0 || actual_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_current_mode == ES8311_MODE_DAC || g_rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2s_channel_read(g_rx_handle, data, len, actual_len, pdMS_TO_TICKS(1000));

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
    }

    return ret;
}
