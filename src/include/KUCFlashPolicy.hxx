#pragma once

#include <cstdint>

// Protocol profiles, not a guarantee of identical programming latency.
// Both use 24-bit addresses, 256-byte pages, WREN 06h, PP 02h and WIP0 in 05h.
// BY25Q128ES has been exercised on OmniSCC+; W25Q128JV is datasheet-qualified.
inline constexpr bool KUCFlashCompatible(uint8_t manufacturer, uint8_t type,
                                         uint8_t density, bool allowUnknown)
{
    // The firmware/FTL is built for 16 MiB. Opt-in never bypasses geometry
    // or accepts a disconnected/floating bus identity.
    if (density != 0x18 || manufacturer == 0 || manufacturer == 0xff
        || type == 0 || type == 0xff) return false;
    const bool known = type == 0x40 && (manufacturer == 0x68 || manufacturer == 0xef);
    return known || allowUnknown;
}
