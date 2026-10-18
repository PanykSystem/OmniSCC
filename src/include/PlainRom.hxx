#pragma once
#include "CompilerAttributes.hxx"
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Plain ROM placement follows the header vectors, not the padded file size.
// In particular, a 48 KiB image with its AB header at offset 4000h starts at 0.
enum class PlainRomMode : uint8_t {
    Mirrored, Normal, Normal0000, Normal4000, Normal8000, Mirrored4000
};

inline bool PlainRomModeForName(const char* name, PlainRomMode& mode)
{
    if (!std::strcmp(name, "Plain") || !std::strcmp(name, "Mirrored")) mode = PlainRomMode::Mirrored;
    else if (!std::strcmp(name, "Normal")) mode = PlainRomMode::Normal;
    else if (!std::strcmp(name, "0x0000")) mode = PlainRomMode::Normal0000;
    else if (!std::strcmp(name, "0x4000")) mode = PlainRomMode::Normal4000;
    else if (!std::strcmp(name, "0x8000")) mode = PlainRomMode::Normal8000;
    else if (!std::strcmp(name, "Mirrored4000")) mode = PlainRomMode::Mirrored4000;
    else return false;
    return true;
}

struct PlainRomLayout {
    uint32_t base = 0x4000;
    uint32_t size = 0;
    bool mirrored = true;

    template<typename Read>
    static PlainRomLayout Make(uint32_t size, PlainRomMode mode, Read read)
    {
        PlainRomLayout out;
        out.size = size;
        out.mirrored = mode == PlainRomMode::Mirrored || mode == PlainRomMode::Mirrored4000;
        if (mode == PlainRomMode::Normal0000) out.base = 0;
        else if (mode == PlainRomMode::Normal8000) out.base = 0x8000;
        else if (mode == PlainRomMode::Normal4000 || mode == PlainRomMode::Mirrored4000) out.base = 0x4000;
        else {
            unsigned votes[3] = {};
            for (uint32_t offset : {0u, 0x4000u}) {
                if (offset + 10 > size || read(offset) != 'A' || read(offset + 1) != 'B') continue;
                for (unsigned i = 0; i < 4; ++i) {
                    const uint32_t addr = read(offset + 2 + 2*i) | uint32_t(read(offset + 3 + 2*i)) << 8;
                    if (!addr) continue;
                    const uint32_t page = (addr >> 14) - (offset >> 14);
                    if (page < 3 && page * 0x4000 + size <= 0x10000) ++votes[page];
                }
            }
            if (votes[1] && votes[1] >= votes[0] && votes[1] >= votes[2]) out.base = 0x4000;
            else if (votes[0] && votes[0] >= votes[2]) out.base = 0;
            else if (votes[2]) out.base = 0x8000;
            else out.base = size > 0x8000 ? 0 : 0x4000;
        }
        return out;
    }

    // All bus-loop users inline this: no flash calls or divisions in the loop.
    OMNISCC_FORCE_INLINE bool Offset(uint16_t address, uint32_t mask, uint32_t& offset) const
    {
        offset = uint32_t(address) - base;
        if (offset < size) return true;
        if (!mirrored) return false;
        if (size && !(size & 0x1fff)) {
            // Preserve all six linear 8 KiB blocks of a 48 KiB ROM. Outside the
            // real window, mirror whole blocks as openMSX RomPlain does.
            const uint32_t page = (uint32_t(address) >> 13) - (base >> 13);
            offset = ((page & ((size >> 13) - 1)) << 13) | (address & 0x1fff);
        } else offset &= mask; // Legacy small/non-aligned ROMs retain FF padding.
        return true;
    }
};
