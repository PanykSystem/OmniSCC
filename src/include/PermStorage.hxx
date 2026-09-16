/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#pragma once

#include <stdbool.h>
#include "Board.h"

uint8_t* flash_get_permanent_storge();
uint32_t flash_get_permanent_storge_size();
void perm_flash_range_erase(uint32_t pos, uint32_t size);
void perm_flash_range_program(uint32_t pos, void* data, uint32_t size);