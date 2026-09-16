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

int _flash_mount();
void _flash_create();
void _flash_sync();
void _flash_fs_read(uint16_t fat_sector, void *buffer);
bool _flash_fs_write(uint16_t fat_sector, const void *buffer);
bool _flash_fs_verify(uint16_t fat_sector, const void *buffer);

#endif