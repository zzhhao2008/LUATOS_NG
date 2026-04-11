/*
 * SDIO port layer for ESP-IDF 5.1
 * Implement low-level SDIO driver functions for ESP32S3
 */

#include "luat_base.h"
#include "luat_sdio.h"
#include "luat_gpio.h"
#include "luat_log.h"

#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

#define LUAT_LOG_TAG "luat_sdio"

// Maximum SDIO instances supported
#define LUAT_SDIO_MAX_INSTANCES 2

// Global SDMMC card instance for each SDIO port
static sdmmc_card_t* sdio_cards[LUAT_SDIO_MAX_INSTANCES] = {NULL, NULL};

// Initialization status for each SDIO port
static uint8_t sdio_initialized[LUAT_SDIO_MAX_INSTANCES] = {0, 0};

// GPIO configuration for each SDIO port (NULL = use default)
static luat_sdio_gpio_config_t* gpio_configs[LUAT_SDIO_MAX_INSTANCES] = {NULL, NULL};

/**
 * Initialize SDIO host and detect SD card
 * @param id SDIO port ID (0 or 1)
 * @return 0 on success, negative on error
 */
int luat_sdio_init(int id) {
    return luat_sdio_init_with_gpio(id, gpio_configs[id]);
}

/**
 * Read sectors from SD card
 * @param id SDIO port ID
 * @param rca Relative Card Address (not used in ESP-IDF implementation)
 * @param buff Buffer to store read data
 * @param offset Starting sector (LBA)
 * @param len Number of sectors to read
 * @return Number of bytes read on success, negative on error
 */
int luat_sdio_sd_read(int id, int rca, char* buff, size_t offset, size_t len) {
    if (id < 0 || id >= LUAT_SDIO_MAX_INSTANCES) {
        LLOGE("Invalid SDIO ID: %d", id);
        return -1;
    }

    if (!sdio_initialized[id] || sdio_cards[id] == NULL) {
        LLOGE("SDIO %d not initialized", id);
        return -2;
    }

    if (buff == NULL) {
        LLOGE("Buffer is NULL");
        return -3;
    }

    // Convert byte offset/len to sector count
    size_t sector = offset / sdio_cards[id]->csd.sector_size;
    size_t count = len / sdio_cards[id]->csd.sector_size;

    esp_err_t ret = sdmmc_read_sectors(sdio_cards[id], buff, sector, count);
    if (ret != ESP_OK) {
        LLOGE("Failed to read sectors: %s (0x%x)", esp_err_to_name(ret), ret);
        return -4;
    }

    return len;
}

/**
 * Write sectors to SD card
 * @param id SDIO port ID
 * @param rca Relative Card Address (not used in ESP-IDF implementation)
 * @param buff Buffer containing data to write
 * @param offset Starting sector (LBA)
 * @param len Number of sectors to write
 * @return Number of bytes written on success, negative on error
 */
int luat_sdio_sd_write(int id, int rca, char* buff, size_t offset, size_t len) {
    if (id < 0 || id >= LUAT_SDIO_MAX_INSTANCES) {
        LLOGE("Invalid SDIO ID: %d", id);
        return -1;
    }

    if (!sdio_initialized[id] || sdio_cards[id] == NULL) {
        LLOGE("SDIO %d not initialized", id);
        return -2;
    }

    if (buff == NULL) {
        LLOGE("Buffer is NULL");
        return -3;
    }

    // Convert byte offset/len to sector count
    size_t sector = offset / sdio_cards[id]->csd.sector_size;
    size_t count = len / sdio_cards[id]->csd.sector_size;

    esp_err_t ret = sdmmc_write_sectors(sdio_cards[id], buff, sector, count);
    if (ret != ESP_OK) {
        LLOGE("Failed to write sectors: %s (0x%x)", esp_err_to_name(ret), ret);
        return -4;
    }

    return len;
}

/**
 * Get SD card information
 * @param id SDIO port ID
 * @param rca Relative Card Address (not used)
 * @return Pointer to sdmmc_card_t structure, NULL on error
 */
sdmmc_card_t* luat_sdio_get_card(int id) {
    if (id < 0 || id >= LUAT_SDIO_MAX_INSTANCES) {
        return NULL;
    }

    if (!sdio_initialized[id]) {
        return NULL;
    }

    return sdio_cards[id];
}

/**
 * Get sector size for the SD card
 * @param id SDIO port ID
 * @return Sector size in bytes, 0 on error
 */
size_t luat_sdio_get_sector_size(int id) {
    if (id < 0 || id >= LUAT_SDIO_MAX_INSTANCES) {
        return 0;
    }

    if (!sdio_initialized[id] || sdio_cards[id] == NULL) {
        return 0;
    }

    return sdio_cards[id]->csd.sector_size;
}

/**
 * Get total sector count for the SD card
 * @param id SDIO port ID
 * @return Total sector count, 0 on error
 */
uint64_t luat_sdio_get_sector_count(int id) {
    if (id < 0 || id >= LUAT_SDIO_MAX_INSTANCES) {
        return 0;
    }

    if (!sdio_initialized[id] || sdio_cards[id] == NULL) {
        return 0;
    }

    return sdio_cards[id]->csd.capacity;
}

/**
 * Set GPIO configuration for SDIO port
 * @param id SDIO port ID (0 or 1)
 * @param config Pointer to GPIO configuration structure (NULL to clear and use default)
 * @return 0 on success, negative on error
 */
int luat_sdio_set_gpio_config(int id, const luat_sdio_gpio_config_t* config) {
    if (id < 0 || id >= LUAT_SDIO_MAX_INSTANCES) {
        LLOGE("Invalid SDIO ID: %d", id);
        return -1;
    }

    if (sdio_initialized[id]) {
        LLOGE("Cannot set GPIO config after initialization for SDIO %d", id);
        return -2;
    }

    if (config != NULL) {
        // Free previous configuration if exists
        if (gpio_configs[id] != NULL) {
            free(gpio_configs[id]);
        }

        // Allocate and copy new configuration
        gpio_configs[id] = (luat_sdio_gpio_config_t*)malloc(sizeof(luat_sdio_gpio_config_t));
        if (gpio_configs[id] == NULL) {
            LLOGE("Failed to allocate memory for GPIO configuration");
            return -3;
        }
        memcpy(gpio_configs[id], config, sizeof(luat_sdio_gpio_config_t));

        LLOGI("SDIO %d GPIO config set: CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d, CD=%d, WP=%d",
                id, config->clk_gpio, config->cmd_gpio, config->d0_gpio,
                config->d1_gpio, config->d2_gpio, config->d3_gpio,
                config->cd_gpio, config->wp_gpio);
    } else {
        // Clear configuration (use default)
        if (gpio_configs[id] != NULL) {
            free(gpio_configs[id]);
            gpio_configs[id] = NULL;
        }
        LLOGI("SDIO %d GPIO config cleared (will use default pins)", id);
    }

    return 0;
}

/**
 * Initialize SDIO with custom GPIO configuration
 * @param id SDIO port ID (0 or 1)
 * @param config Pointer to GPIO configuration structure (NULL to use default)
 * @return 0 on success, negative on error
 */
int luat_sdio_init_with_gpio(int id, const luat_sdio_gpio_config_t* config) {
    if (id < 0 || id >= LUAT_SDIO_MAX_INSTANCES) {
        LLOGE("Invalid SDIO ID: %d", id);
        return -1;
    }

    // Check if already initialized
    if (sdio_initialized[id] && sdio_cards[id] != NULL) {
        LLOGI("SDIO %d already initialized", id);
        return 0;
    }

    esp_err_t ret;

    // Configure SDMMC host
    sdmmc_host_t host_config = SDMMC_HOST_DEFAULT();

    // Initialize SDMMC host
    ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        LLOGE("Failed to initialize SDMMC host for SDIO %d: %s (0x%x)", id, esp_err_to_name(ret), ret);
        return -2;
    }

    // Configure SDMMC slot
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // Set 4-bit data bus for better performance
    slot_config.width = 4;

    // Apply custom GPIO configuration if provided
    if (config != NULL) {
        slot_config.clk = config->clk_gpio;
        slot_config.cmd = config->cmd_gpio;
        slot_config.d0 = config->d0_gpio;
        slot_config.d1 = config->d1_gpio;
        slot_config.d2 = config->d2_gpio;
        slot_config.d3 = config->d3_gpio;

        // Optional pins (set to -1 if not used)
        if (config->cd_gpio >= 0) {
            slot_config.cd = config->cd_gpio;
        }
        if (config->wp_gpio >= 0) {
            slot_config.wp = config->wp_gpio;
        }

        LLOGI("Using custom GPIO configuration for SDIO %d", id);
    } else {
        LLOGI("Using default GPIO configuration for SDIO %d", id);
    }

    // Allocate card structure
    sdio_cards[id] = (sdmmc_card_t*)malloc(sizeof(sdmmc_card_t));
    if (sdio_cards[id] == NULL) {
        LLOGE("Failed to allocate memory for SD card structure");
        sdmmc_host_deinit();
        return -3;
    }

    // Initialize the card
    ret = sdmmc_card_init(&host_config, sdio_cards[id]);
    if (ret != ESP_OK) {
        LLOGE("Failed to initialize SD card: %s (0x%x)", esp_err_to_name(ret), ret);
        free(sdio_cards[id]);
        sdio_cards[id] = NULL;
        sdmmc_host_deinit();
        return -4;
    }

    // Mark as initialized
    sdio_initialized[id] = 1;

    LLOGI("SDIO %d initialized successfully", id);
    LLOGI("  Capacity: %llu MB", (uint64_t)(sdio_cards[id]->csd.capacity / (1024 * 1024)));
    LLOGI("  Sector size: %d", sdio_cards[id]->csd.sector_size);

    return 0;
}
