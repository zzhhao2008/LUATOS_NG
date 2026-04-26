#ifndef _ES8311_DRIVER_H_
#define _ES8311_DRIVER_H_

#include "driver/gpio.h"
#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/i2s_types.h"
#include "esp_log.h"

#define ES8311_I2C_ADDR    0x18

// ES8311 寄存器定义
#define ES8311_RESET_REG00              0x00
#define ES8311_CLK_MANAGER_REG01        0x01
#define ES8311_CLK_MANAGER_REG02        0x02
#define ES8311_CLK_MANAGER_REG03        0x03
#define ES8311_CLK_MANAGER_REG04        0x04
#define ES8311_CLK_MANAGER_REG05        0x05
#define ES8311_CLK_MANAGER_REG06        0x06
#define ES8311_CLK_MANAGER_REG07        0x07
#define ES8311_CLK_MANAGER_REG08        0x08
#define ES8311_SDPIN_REG09              0x09
#define ES8311_SDPOUT_REG0A             0x0A
#define ES8311_SYSTEM_REG0B             0x0B
#define ES8311_SYSTEM_REG0C             0x0C
#define ES8311_SYSTEM_REG0D             0x0D
#define ES8311_SYSTEM_REG0E             0x0E
#define ES8311_SYSTEM_REG0F             0x0F
#define ES8311_SYSTEM_REG10             0x10
#define ES8311_SYSTEM_REG11             0x11
#define ES8311_SYSTEM_REG12             0x12
#define ES8311_SYSTEM_REG13             0x13
#define ES8311_SYSTEM_REG14             0x14
#define ES8311_ADC_REG15                0x15
#define ES8311_ADC_REG16                0x16
#define ES8311_ADC_REG17                0x17
#define ES8311_ADC_REG18                0x18
#define ES8311_ADC_REG19                0x19
#define ES8311_ADC_REG1A                0x1A
#define ES8311_ADC_REG1B                0x1B
#define ES8311_ADC_REG1C                0x1C
#define ES8311_DAC_REG31                0x31
#define ES8311_DAC_REG32                0x32
#define ES8311_DAC_REG33                0x33
#define ES8311_DAC_REG34                0x34
#define ES8311_DAC_REG35                0x35
#define ES8311_DAC_REG37                0x37
#define ES8311_GPIO_REG44               0x44
#define ES8311_GP_REG45                 0x45
#define ES8311_CHD1_REGFD               0xFD
#define ES8311_CHD2_REGFE               0xFE
#define ES8311_CHVER_REGFF              0xFF

// ES8311音频模式
typedef enum {
    ES8311_MODE_DAC = 0,      // 仅DAC(播放)
    ES8311_MODE_ADC = 1,      // 仅ADC(录制)
    ES8311_MODE_BOTH = 2      // 同时播放和录制
} es8311_audio_mode_t;

// ES8311配置结构
typedef struct {
    gpio_num_t scl;           // SCL引脚
    gpio_num_t sda;           // SDA引脚
    gpio_num_t mclk;          // MCLK引脚
    gpio_num_t sclk;          // SCLK引脚 (I2S BCLK)
    gpio_num_t asdout;        // ASDOUT引脚 (I2S DAC输出)
    gpio_num_t lrck;          // LRCK引脚 (I2S LRCK)
    gpio_num_t dsin;          // DSIN引脚 (I2S ADC输入)
    uint32_t fre;            // I2C频率
    i2c_port_t i2c_num;     // I2C端口号
} es8311_cfg_t;

// 音频数据回调函数类型
typedef void (*es8311_audio_callback_t)(uint8_t *data, size_t len, void *user_data);

/**
 * @brief 初始化ES8311
 * @param cfg 配置参数
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_init(es8311_cfg_t *cfg);

/**
 * @brief 反初始化ES8311
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_deinit(void);

/**
 * @brief 设置采样率
 * @param sample_rate 采样率(8000, 16000, 44100, 48000等)
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_set_sample_rate(uint32_t sample_rate);

/**
 * @brief 设置音频模式
 * @param mode 音频模式 (ES8311_MODE_DAC/ES8311_MODE_ADC/ES8311_MODE_BOTH)
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_set_mode(es8311_audio_mode_t mode);

/**
 * @brief 设置音量
 * @param volume 音量(0-100)
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_set_volume(uint8_t volume);

/**
 * @brief 获取音量
 * @param volume 输出音量指针
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_get_volume(uint8_t *volume);

/**
 * @brief 设置静音
 * @param enable 1静音，0取消静音
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_set_mute(uint8_t enable);

/**
 * @brief 获取静音状态
 * @param enabled 输出静音状态指针
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_get_mute(uint8_t *enabled);

/**
 * @brief 获取芯片信息
 * @param chip_id 芯片ID
 * @param version 版本号
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_get_info(uint8_t *chip_id, uint8_t *version);

/**
 * @brief 开始音频播放/录制
 * @param mode 音频模式
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_start(es8311_audio_mode_t mode);

/**
 * @brief 停止音频播放/录制
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_stop(void);

/**
 * @brief 写入音频数据(用于播放)
 * @param data 音频数据指针
 * @param len 数据长度
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_write_audio(uint8_t *data, size_t len);

/**
 * @brief 读取音频数据(用于录制)
 * @param data 接收数据缓冲区
 * @param len 期望读取的长度
 * @param actual_len 实际读取的长度
 * @return ESP_OK 成功，其他失败
 */
esp_err_t es8311_read_audio(uint8_t *data, size_t len, size_t *actual_len);

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ES8311_DRIVER_H_ */
