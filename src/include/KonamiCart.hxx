/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#pragma once

#include "Cartridge.hxx"


class CKonamiCartridge: public CCartridge
{
public:
    CKonamiCartridge(COmniSCC& multiCart, uint8_t* data, uint32_t sizeMask);
    CKonamiCartridge(COmniSCC& multiCart, const RomStorage& storage);
    ~CKonamiCartridge();

private:
    template<RomStorageKind Kind> __attribute__((noinline, section(".time_critical.KonamiCart_storage"))) bool ExecuteStorage();
    virtual bool Execute();
};