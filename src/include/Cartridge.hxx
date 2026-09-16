/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#pragma once

#include "Board.h"

extern uint8_t mem4000[0x2000*0x10];  // 128K


class COmniSCC;

class CCartridge
{
public:
    // sizeMask bounds every index into `data`. It is (power-of-two >= ROM size) - 1,
    // capped to the buffer the ROM was loaded into. Masking with it makes an
    // out-of-range bank register mirror inside the ROM instead of reading past the
    // end of car_cache (unmapped SRAM -> bus fault) or past the permanent flash region.
    CCartridge(COmniSCC& multiCart, uint8_t* data, uint32_t sizeMask);
    ~CCartridge();
    virtual bool Execute();

    // Build a sizeMask for a `size`-byte ROM living in a `limit`-byte buffer.
    // `limit` must be a power of two.
    static uint32_t MakeSizeMask(uint32_t size, uint32_t limit);

protected:
    uint8_t *_data;
    uint32_t _mask;
    COmniSCC& _multiCart;
};