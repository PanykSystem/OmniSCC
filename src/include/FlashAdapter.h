/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#ifndef _FS_ADOPT_H
#define _FS_ADOPT_H

#include "Board.h"
#include "hardware/flash.h"

#define FLASH_RESERVED_SECTORS 16
#define FAT_SECTOR_SIZE 512
#define NUM_FLASH_SECTORS (FS_SIZE/FLASH_SECTOR_SIZE)
#define FAT_NUM_SECTORS ((NUM_FLASH_SECTORS-FLASH_RESERVED_SECTORS)*(FLASH_SECTOR_SIZE/FAT_SECTOR_SIZE))

bool _flash_is_blank(void);
bool flash_get_xip_page(uint32_t logicalSector, uint32_t* address);
typedef enum { FLASH_LAYOUT_CURRENT, FLASH_LAYOUT_LEGACY, FLASH_LAYOUT_FORMATTING,
               FLASH_LAYOUT_BLANK, FLASH_LAYOUT_UNKNOWN } FlashLayoutState;
FlashLayoutState _flash_layout_state(void);
bool _flash_finish_format(void);
int _flash_mount();
bool _flash_create();
void _flash_sync();
bool _flash_sync_checked(void);
void _flash_fs_read(uint16_t fat_sector, void *buffer);
bool _flash_fs_write(uint16_t fat_sector, const void *buffer);
bool _flash_fs_verify(uint16_t fat_sector, const void *buffer);

#endif
