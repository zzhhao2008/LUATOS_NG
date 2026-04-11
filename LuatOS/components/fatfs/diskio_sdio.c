/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2016        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "luat_base.h"
#include "luat_sdio.h"

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

#define LUAT_LOG_TAG "fatfs"
#include "luat_log.h"

// Storage for the active SDIO ID
static int active_sdio_id = -1;

/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS sdio_initialize (
	void* userdata
)
{
	if (userdata == NULL) {
		return RES_NOTRDY;
	}

	luat_fatfs_sdio_t* sdio = (luat_fatfs_sdio_t*)userdata;

	int ret = luat_sdio_init(sdio->id);
	if (ret != 0) {
		LLOGE("sdio_initialize failed: %d", ret);
		return STA_NOINIT;
	}

	// Store the active SDIO ID
	active_sdio_id = sdio->id;

	return RES_OK;
}


/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS sdio_status (
	void* userdata
)
{
	if (userdata == NULL) {
		return STA_NOINIT;
	}

	luat_fatfs_sdio_t* sdio = (luat_fatfs_sdio_t*)userdata;

	// Check if SDIO is initialized by trying to get card info
	sdmmc_card_t* card = luat_sdio_get_card(sdio->id);
	if (card == NULL) {
		return STA_NOINIT;
	}

	return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/


DRESULT sdio_read (
	void* userdata,
	BYTE *buff,			/* Pointer to the data buffer to store read data */
	DWORD sector,		/* Start sector number (LBA) */
	UINT count			/* Sector count (1..128) */
)
{
	if (userdata == NULL) {
		return RES_NOTRDY;
	}

	if (buff == NULL) {
		return RES_PARERR;
	}

	if (count == 0) {
		return RES_OK;
	}

	luat_fatfs_sdio_t* sdio = (luat_fatfs_sdio_t*)userdata;

	// Get sector size
	size_t sector_size = luat_sdio_get_sector_size(sdio->id);
	if (sector_size == 0) {
		return RES_ERROR;
	}

	// Calculate offset in bytes
	size_t offset = sector * sector_size;
	size_t len = count * sector_size;

	// Read sectors
	int ret = luat_sdio_sd_read(sdio->id, sdio->rca, (char*)buff, offset, len);
	if (ret <= 0) {
		LLOGE("sdio_read failed: sector=%lu, count=%u, ret=%d", sector, count, ret);
		return RES_ERROR;
	}

	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT sdio_write (
	void* userdata,
	const BYTE *buff,	/* Pointer to the data to be written */
	DWORD sector,		/* Start sector number (LBA) */
	UINT count			/* Sector count (1..128) */
)
{
	if (userdata == NULL) {
		return RES_NOTRDY;
	}

	if (buff == NULL) {
		return RES_PARERR;
	}

	if (count == 0) {
		return RES_OK;
	}

	luat_fatfs_sdio_t* sdio = (luat_fatfs_sdio_t*)userdata;

	// Get sector size
	size_t sector_size = luat_sdio_get_sector_size(sdio->id);
	if (sector_size == 0) {
		return RES_ERROR;
	}

	// Calculate offset in bytes
	size_t offset = sector * sector_size;
	size_t len = count * sector_size;

	// Write sectors
	int ret = luat_sdio_sd_write(sdio->id, sdio->rca, (char*)buff, offset, len);
	if (ret <= 0) {
		LLOGE("sdio_write failed: sector=%lu, count=%u, ret=%d", sector, count, ret);
		return RES_ERROR;
	}

	return RES_OK;
}


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/


DRESULT sdio_ioctl (
	void* userdata,
	BYTE ctrl,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	if (userdata == NULL) {
		return RES_NOTRDY;
	}

	luat_fatfs_sdio_t* sdio = (luat_fatfs_sdio_t*)userdata;

	switch (ctrl) {
		case GET_SECTOR_COUNT: {
			// Return total number of sectors on the disk
			uint64_t sector_count = luat_sdio_get_sector_count(sdio->id);
			if (sector_count == 0) {
				return RES_ERROR;
			}
			*(LBA_t*)buff = (LBA_t)sector_count;
			break;
		}

		case GET_SECTOR_SIZE: {
			// Return sector size (in bytes)
			size_t sector_size = luat_sdio_get_sector_size(sdio->id);
			if (sector_size == 0) {
				return RES_ERROR;
			}
			*(WORD*)buff = (WORD)sector_size;
			break;
		}

		case GET_BLOCK_SIZE: {
			// Return erase block size (in sectors)
			// For SD cards, this is typically the allocation unit
			*(DWORD*)buff = 1; // 1 sector
			break;
		}

		case CTRL_SYNC: {
			// Make sure that data has been written to the disk
			// ESP-IDF's sdmmc_write_sectors ensures data is written
			break;
		}

		default:
			return RES_PARERR;
	}

	return RES_OK;
}

const block_disk_opts_t sdio_disk_opts = {
    .initialize = sdio_initialize,
    .status = sdio_status,
    .read = sdio_read,
    .write = sdio_write,
    .ioctl = sdio_ioctl,
};

// Function to set SDHC controller binding - this is the weak function that can be overridden
#ifndef LUAT_COMPILER_NOWEAK
LUAT_WEAK void luat_sdio_set_sdhc_ctrl(block_disk_t *disk)
{
	// Default implementation does nothing
	// Platform-specific implementations can override this
}
#else
void luat_sdio_set_sdhc_ctrl(block_disk_t *disk);
#endif

static block_disk_t disk = {0};

DRESULT diskio_open_sdio(BYTE pdrv, luat_fatfs_sdio_t* userdata) {
	// 暂时只支持单个fatfs实例
	disk.opts = &sdio_disk_opts;
    disk.userdata = userdata;
    luat_sdio_set_sdhc_ctrl(&disk);
	return diskio_open(pdrv, &disk);
}

//static DWORD get_fattime() {
//	how to get?
//}

//--------------------------------------------------------------------------------------

