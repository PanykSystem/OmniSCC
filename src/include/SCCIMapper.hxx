#pragma once
#include "CompilerAttributes.hxx"
#include <cstdint>

// Expanded 128 KiB SCC-I RAM cartridge. Register semantics follow openMSX's
// MSXSCCPlusCart (src/sound): BFFE/BFFF always win, then RAM-write protection,
// bank registers, and finally SCC writes. Reads can still see SCC in RAM mode.
// SCCPlus launches converted ROMs in enhanced mode; SCCI starts compatible.
struct SCCIMapper {
    uint8_t banks[4] = {};
    uint8_t mode = 0;

    void Reset(bool enhanced) {
        banks[0] = 0; banks[1] = 1; banks[2] = 2; banks[3] = 3;
        mode = enhanced ? 0x20 : 0;
    }
    OMNISCC_FORCE_INLINE bool PlusMode() const { return (mode & 0x20) != 0; }
    OMNISCC_FORCE_INLINE bool IsSound(uint16_t address) const {
        if (PlusMode()) return (banks[3] & 0x80) && address >= 0xb800 && address < 0xc000;
        return (banks[2] & 0x3f) == 0x3f && address >= 0x9800 && address < 0xa000;
    }
    OMNISCC_FORCE_INLINE uint32_t MemoryOffset(uint16_t address) const {
        return (uint32_t(banks[(address >> 13) - 2] & 15) << 13) | (address & 0x1fff);
    }
    OMNISCC_FORCE_INLINE bool RamWriteEnabled(uint16_t address) const {
        if (address < 0x4000 || address >= 0xc000) return false;
        if (mode & 0x10) return true;
        const unsigned region = (address >> 13) - 2;
        return (region == 0 && (mode & 1)) || (region == 1 && (mode & 2))
            || (region == 2 && (mode & 0x24) == 0x24);
    }
    // True means the caller must synchronously enqueue a sound-register write.
    OMNISCC_FORCE_INLINE bool Write(uint16_t address, uint8_t value, uint8_t* ram) {
        if (address < 0x4000 || address >= 0xc000) return false;
        if ((address & 0xfffe) == 0xbffe) { mode = value; return false; }
        if (RamWriteEnabled(address)) { ram[MemoryOffset(address)] = value; return false; }
        if ((address & 0x1800) == 0x1000) {
            banks[(address >> 13) - 2] = value;
            return false;
        }
        return IsSound(address);
    }
};
