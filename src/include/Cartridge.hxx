/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#pragma once

#include "Board.h"
#include "RomStorage.hxx"
#include "PlainRom.hxx"

extern uint8_t mem4000[0x2000*0x10];  // 128K


class COmniSCC;

class CCartridge
{
public:
    // sizeMask bounds every index into `data`. It is (power-of-two >= ROM size) - 1,
    // capped to the buffer the ROM was loaded into. Masking with it makes an
    // out-of-range bank register mirror inside the ROM instead of reading past the
    // end of the allocated RAM ROM buffer. Mapped storage also checks EOF.
    CCartridge(COmniSCC& multiCart, uint8_t* data, uint32_t sizeMask);
    CCartridge(COmniSCC& multiCart, const RomStorage& storage, PlainRomMode mode = PlainRomMode::Mirrored);
    virtual ~CCartridge();
    template<RomStorageKind Kind> __attribute__((noinline, section(".time_critical.Cartridge_storage"))) bool ExecuteStorage();
    virtual bool Execute();

    // Build a sizeMask for a `size`-byte ROM living in a `limit`-byte buffer.
    // `limit` must be a power of two.
    static uint32_t MakeSizeMask(uint32_t size, uint32_t limit);

protected:
    PlainRomLayout _plain;
    RomStorage _storage;
    uint8_t *_data;
    uint32_t _mask;
    COmniSCC& _multiCart;
};

// Generic 8 KiB switches every window, including 4000h, on any write in it.
// Unlike Konami/ASCII8, it has neither a fixed first bank nor special registers.
class CGeneric8kBCartridge : public CCartridge
{
public:
    CGeneric8kBCartridge(COmniSCC& manager, const RomStorage& storage)
        : CCartridge(manager, storage) {}
    bool Execute() override;
private:
    template<RomStorageKind Kind> __attribute__((noinline, section(".time_critical.Generic8kB_storage"))) bool ExecuteStorage();
};
