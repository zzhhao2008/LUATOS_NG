#ifndef LUAT_SDIO_H
#define LUAT_SDIO_H
#include "luat_base.h"

typedef struct luat_sdio
{
    /* data */
    int  id;      // id
    int rca;      // id
} luat_sdio_t;

typedef struct luat_fatfs_sdio
{
    int  id;      // id
    int rca;      // id
}luat_fatfs_sdio_t;

// SDIO GPIO configuration structure
typedef struct luat_sdio_gpio_config
{
    int clk_gpio;    // Clock GPIO pin
    int cmd_gpio;    // Command GPIO pin
    int d0_gpio;     // Data 0 GPIO pin
    int d1_gpio;     // Data 1 GPIO pin (4-bit mode)
    int d2_gpio;     // Data 2 GPIO pin (4-bit mode)
    int d3_gpio;     // Data 3 GPIO pin (4-bit mode)
    int cd_gpio;     // Card detect GPIO pin (optional, set to -1 if not used)
    int wp_gpio;     // Write protect GPIO pin (optional, set to -1 if not used)
}luat_sdio_gpio_config_t;

// Forward declaration for sdmmc_card_t (ESP-IDF already defines this type)
#ifdef __cplusplus
extern "C" {
#endif

struct sdmmc_card_t;

#ifdef __cplusplus
}
#endif

int luat_sdio_init(int id);
int luat_sdio_sd_read(int id, int rca, char* buff, size_t offset, size_t len);
int luat_sdio_sd_write(int id, int rca, char* buff, size_t offset, size_t len);
int luat_sdio_sd_mount(int id, int *rca, char* path,int auto_format);
int luat_sdio_sd_unmount(int id, int rca);
int luat_sdio_sd_format(int id, int rca);

// ESP-IDF 5.1 specific functions
sdmmc_card_t* luat_sdio_get_card(int id);
size_t luat_sdio_get_sector_size(int id);
uint64_t luat_sdio_get_sector_count(int id);

// GPIO configuration functions
int luat_sdio_set_gpio_config(int id, const luat_sdio_gpio_config_t* config);
int luat_sdio_init_with_gpio(int id, const luat_sdio_gpio_config_t* config);

#endif
