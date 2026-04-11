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

// Forward declaration for sdmmc_card_t
#ifdef __cplusplus
extern "C" {
#endif

struct sdmmc_card_t;
typedef struct sdmmc_card_t sdmmc_card_t;

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

#endif
