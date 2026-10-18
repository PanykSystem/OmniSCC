/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#include "ff.h"
#include "diskio.h"
#include "FSAdapter.h"
#include "VolumeAccess.h"
#include "FlashAdapter.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"

bool flashfs_is_mounted = false;

// The one FatFs volume object. It MUST outlive every f_open/f_read/f_write:
// f_mount() stores this pointer in FatFs's volume table, it does not copy the
// object. A stack-local FATFS here (as _fs_mount once had) leaves that table
// pointing into reclaimed stack -- fine until a deeper call overwrites the
// ~560-byte object (win[] cache, fs_type mount flag), after which FatFs reads
// garbage and access "randomly" dies under load. The USB MSC path never
// noticed because it bypasses FatFs entirely (raw _flash_fs_* sectors).
static FATFS g_fatfs;

#define WELCOME_TXT "OmniSCC\r\n(c)2026 OSPC.club\r\nDrag ROM files in here!\r\n"

static bool fs_create_failed(void);

bool _fs_initialize(void)
{
    if (volume_owner() != VOLUME_MENU) return false;
    // Keep raw-device readiness separate from FatFs registration. A valid
    // flash map with an unreadable FAT must still be usable by USB MSC.
    volume_set_ready(false);
    flashfs_is_mounted = false;
    f_mount(0, "", 0);
    if (_fs_mount()) return true;
    if (volume_is_ready()) return false;  // valid map: let the USB host repair FAT

    // No usable map exists (blank/legacy/interrupted/corrupt metadata).
    // Recreating it loses the old logical sectors; build a clean FAT as well.
    return _fs_create();
}

static bool fs_create_failed(void)
{
    f_mount(0, "", 0);
    flashfs_is_mounted = false;
    volume_set_ready(false);
    return false;
}

bool _fs_create()
{
    if (volume_owner() != VOLUME_MENU) return false;
    volume_set_ready(false);
    flashfs_is_mounted = false;
    f_mount(0, "", 0);
    if (!_flash_create()) return false;
    volume_set_ready(true);
    flashfs_is_mounted = true;

    // now create a fatfs on the flash_fs filesystem :-)

    FIL fil;            /* File object */
    FRESULT res;        /* API result code */
    BYTE work[FF_MAX_SS]; /* Work area (larger is better for processing time) */

    /* Format the default drive with default parameters */
    printf("making fatfs\n");
    static const MKFS_PARM defopt = {FM_FAT, 0, 0, 0, 0};

    res = f_mkfs("", &defopt, work, sizeof work);
    if(res != FR_OK)
        return fs_create_failed();
    res = f_mount(&g_fatfs, "", 0);
    if(res != FR_OK)
        return fs_create_failed();
    res = f_setlabel("OmniSCC");
    if(res != FR_OK)
        return fs_create_failed();
    res = f_open(&fil, "WELCOME.TXT", FA_CREATE_NEW | FA_WRITE);
    if(res != FR_OK)
        return fs_create_failed();
    const int written = f_puts(WELCOME_TXT, &fil);
    res = f_close(&fil);
    if(res != FR_OK || written < 0)
        return fs_create_failed();

    // Leave the volume mounted on g_fatfs for the rest of the session. This
    // used to unmount here (f_mount(0,...)); since _fs_create runs only on a
    // first boot (no valid volume yet), that left nothing mounted until the
    // next reboot and the cartridge's f_open calls failed. f_close above
    // already issued CTRL_SYNC, so the sector map is persisted regardless.
    if (!_flash_finish_format()) return fs_create_failed();
    return true;
}


bool _fs_mount()
{
    if (volume_owner() != VOLUME_MENU) return false;
    int err = _flash_mount();
    if (err)
        return false;

    volume_set_ready(true);
    flashfs_is_mounted = true;
    const bool ok = f_mount(&g_fatfs, "", 1) == FR_OK;
    if (!ok) { f_mount(0, "", 0); flashfs_is_mounted = false; }
    return ok;
}

bool _fs_is_mounted() { return flashfs_is_mounted; }

bool _fs_unmount(void)
{
    if (f_mount(0, "", 0) != FR_OK) return false;
    flashfs_is_mounted = false;
    return true;
}

uint32_t _fs_read(uint8_t* buff, uint32_t sector, uint32_t count)
{
    if (!flashfs_is_mounted) return RES_ERROR;
    return volume_read(VOLUME_MENU, buff, sector, count);
}

uint32_t _fs_write(const uint8_t* buff, uint32_t sector, uint32_t count)
{
    if (!flashfs_is_mounted) return RES_ERROR;
    return volume_write(VOLUME_MENU, buff, sector, count);
}

void _fs_sync()
{
    _flash_sync();
}

//// fatfs integration

DRESULT disk_read (
    BYTE drv,       /* Physical drive number (0) */
    BYTE* buff,     /* Pointer to the data buffer to store read data */
    LBA_t sector,   /* Start sector number (LBA) */
    UINT count      /* Number of sectors to read (1..128) */
)
{
    if (drv) return RES_PARERR;
    return _fs_read(buff, sector, count);
}

DRESULT disk_write (
    BYTE drv,           /* Physical drive number (0) */
    const BYTE* buff,   /* Ponter to the data to write */
    LBA_t sector,       /* Start sector number (LBA) */
    UINT count          /* Number of sectors to write (1..128) */
)
{
    if (drv) return RES_PARERR;
    return _fs_write(buff, sector, count);
}

DRESULT disk_ioctl (
    BYTE drv,       /* Physical drive number (0) */
    BYTE cmd,       /* Control command code */
    void* buff      /* Pointer to the conrtol data */
)
{
    if (drv) return RES_PARERR;
    DRESULT res = RES_OK;

    switch (cmd) {
    case CTRL_SYNC :        /* Wait for end of internal write process of the drive */
        _fs_sync();         /* flush the sector map to flash */
        break;

    case GET_SECTOR_COUNT : /* Get drive capacity in unit of sector (DWORD) */
        *(DWORD*)buff = FAT_NUM_SECTORS;
        break;

    case GET_BLOCK_SIZE :   /* Get erase block size in unit of sector (DWORD) */
        *(DWORD*)buff = FAT_SECTOR_SIZE;
        break;

    default:
        res = RES_PARERR;
    }

    return res;
}

DSTATUS disk_status (
    BYTE drv        /* Physical drive number (0) */
)
{
    if (drv /*|| !flashfs_is_mounted*/) return STA_NOINIT;
    return 0;  
}

DSTATUS disk_initialize (
    BYTE drv        /* Physical drive number (0) */
)
{
    return disk_status(drv);
}

