/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */


#ifndef __FS_ADOPT_H__
#define __FS_ADOPT_H__

#include "FSAdapter.h"
#include <stdbool.h>

bool _fs_create();
bool _fs_mount();
bool _fs_is_mounted();
uint32_t _fs_read(uint8_t* buff, uint32_t sector, uint32_t count);
uint32_t _fs_write(const uint8_t* buff, uint32_t sector, uint32_t count);
void _fs_sync();

#endif