/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "PermStorage.hxx"

/* Low level flash functions */

uint8_t* flash_get_permanent_storge()
{
    return (uint8_t*)(XIP_BASE + PERM_BASE);
}

uint32_t flash_get_permanent_storge_size()
{
    return PERM_BYTES;
}

void perm_flash_range_erase(uint32_t pos, uint32_t size)
{
    if((pos + size) > PERM_BYTES)
        return;
    flash_range_erase(pos + PERM_BASE, size);
}

void perm_flash_range_program(uint32_t pos, void* data, uint32_t size)
{
    if((pos + size) > PERM_BYTES)
        return;
    flash_range_program(pos + PERM_BASE, (uint8_t*) data, size);
}
