/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#pragma once

#include "Cartridge.hxx"


class CASCII16Cartridge: public CCartridge
{
public:
    CASCII16Cartridge(COmniSCC& multiCart, uint8_t* data, uint32_t sizeMask);
    CASCII16Cartridge(COmniSCC& multiCart, const RomStorage& storage);
    ~CASCII16Cartridge();

private:
    template<RomStorageKind Kind> __attribute__((noinline, section(".time_critical.ASCII16Cart_storage"))) bool ExecuteStorage();
    virtual bool Execute();
};
