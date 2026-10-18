#pragma once
#include <stdint.h>

// CMD_MAP_FILE / CMD_MAP_DISK results. Keep in sync with msx/mount_errors.inc.
// Nonzero codes must never equal the loader ready sentinel (0xA5).
enum class MountResult : uint8_t {
    Ok = 0,
    RomMissing = 0x20,
    DiskMissing = 0x21,
    RomSize = 0x22,
    DiskSize = 0x23,
    DiskRomMissing = 0x24,
    DiskRomSize = 0x25,
    UnsupportedDiskRom = 0x26,
    DiskRomPatch = 0x27,
    FileRead = 0x28,
    DiskRomRead = 0x29, // same message as FileRead, but identifies the dump path
    UnsupportedMapper = 0x2A,
    SaveNoSpace = 0x2B,
    SavePrepare = 0x2C,
    Unknown = 0xFE,
};

// Missing file or missing directory; other FatFs failures are storage errors.
inline bool MountFileMissing(unsigned result) { return result == 4 || result == 5; }
