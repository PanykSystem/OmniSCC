#pragma once
#include <cstdint>
#include <cstddef>
#include "pico/stdlib.h"

enum class RomStorageKind : uint8_t { Ram, Mapped };
struct RomStorage {
    RomStorageKind kind = RomStorageKind::Ram;
    uint32_t size = 0, mask = 0, pageCount = 0;
    const uint8_t* ram = nullptr;
    const uint32_t* pages = nullptr;
    template<RomStorageKind Kind>
    __attribute__((always_inline)) inline uint8_t Read(uint32_t offset) const {
        offset &= mask;
        if constexpr (Kind == RomStorageKind::Ram) return ram[offset];
        else {
            if (offset >= size) return 0xff;
            return *(const volatile uint8_t*)(uintptr_t(pages[offset >> 9]) + (offset & 511));
        }
    }
    uint8_t ReadAny(uint32_t offset) const;
};
inline bool MakeRomMask(uint32_t size, uint32_t& mask) {
    if (!size || size > 0x80000000u) return false;
    uint32_t power = 1;
    while (power < size) power <<= 1;
    mask = power - 1; return true;
}
struct RomWorkspace {
    size_t table, tail, scratch, visited, total;
};
inline bool RomWorkspaceLayout(uint32_t size, uint32_t clusterCount, size_t capacity, RomWorkspace& out) {
    uint32_t mask;
    if (!MakeRomMask(size,mask)) return false;
    const uint64_t table = ((uint64_t(size)+511)/512)*4;
    const uint64_t total = table + 1024 + (uint64_t(clusterCount)+7)/8;
    if (total > capacity) return false;
    out = {0,size_t(table),size_t(table+512),size_t(table+1024),size_t(total)};
    return true;
}
