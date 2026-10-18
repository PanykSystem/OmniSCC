/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#pragma once

extern "C"
{
#include "ff.h"
}

#include "Cartridge.hxx"
#include "MountResult.hxx"
#include "DiskImage.hxx"
#include "DiskFiles.hxx"
#include "DiskOptions.h"
#include "RomDatabaseOptions.h"
#include "KUCOptions.h"

enum class NextorBootResult : uint8_t { Ok, MissingRom, InvalidRom, UnsupportedVolume, StorageError };

class CSCCSound;

class COmniSCC
{
public:
    enum CartType
    {
        CT_Plain = 0,
        CT_Loader,
        CT_PAGE2,
        CT_MIRRORED,
        CT_Konami,
        CT_KonamiSCC,
        CT_ASCII16X,
        CT_NE08,
        CT_NE16,
        CT_ASCII8,
        CT_ASCII16,
        CT_KUC,
        CT_Generic8kB,
        CT_Normal,
        CT_Normal0000,
        CT_Normal4000,
        CT_Normal8000,
        CT_Mirrored4000,
        CT_SCCI,
        CT_SCCPlus,
        CT_LLast
    };

    COmniSCC();
    ~COmniSCC();

    bool Start();
    uint8_t DiskOptions() const { return _diskOptions; }
    bool SetDiskOptions(uint8_t flags) {
        if (flags & ~DISK_OPTION_MULTIDISK) return false;
        _diskOptions = flags;
        return true;
    }
    uint8_t KUCOptions() const { return _kucOptions; }
    bool SetKUCOptions(uint8_t flags) {
        if (flags & ~KUC_OPTION_ALLOW_UNKNOWN_FLASH) return false;
        _kucOptions = flags;
        return true;
    }
    bool Mount(const char* fileName);
    bool PrepareROM(char* path, size_t capacity, CartType& type);
    bool MountPrepared(const char* fileName, CartType type);
    bool SetRomDatabasePath(const char* path);
    bool MountLoader();
    bool MountSCC();
    NextorBootResult MountNextor(const char* romPath);

    // Load and patch a Disk ROM dump, mount `imagePath`, and make a disk
    // interface the current cartridge. Fails if either file is missing or the
    // dump cannot be patched.
    //
    // `romPath` names the dump; null or empty means `_diskROMName`. The menu
    // passes loader.ini's diskrom= through here, so an image can be paired with
    // the dump from the machine it is meant to run on -- the National and
    // Yamaha dumps differ in both Disk BASIC and controller addresses.
    // `imagePath2` is the image for the second drive, from loader.ini's disk2=.
    // Null, empty, or an image that will not mount leaves drive B empty, which
    // the MSX sees as a drive with no disk in it.
    bool MountDisk(const char* imagePath, const char* romPath = nullptr,
                   const char* imagePath2 = nullptr);

    MountResult LastMountResult() const { return _mountResult; }

    bool Mounted() { return _current != 0; };

private:
    uint8_t _diskOptions = DISK_OPTIONS_DEFAULT;
    uint8_t _kucOptions = 0;

    static constexpr const char* _diskROMName = "disk.rom";

    CDiskImage _diskImage;          // drive A
    CDiskImage _diskImage2;         // drive B, empty unless disk2= named one
    // COmniSCC lives on core 0's 16 KiB stack. Keep the two transfer handles
    // here so the heap needs only the small cartridge object during boot.
    CDiskFiles _diskFiles;

    CartType DetectCartType(const RomStorage& storage);
    static bool MapperCanAddress(CartType type, uint32_t size);

    // Copies the loader/menu ROM into its own buffer and makes it current.
    // Keeps the running menu alive when a later Mount() overwrites car_cache.
    bool MountLoaderROM(const uint8_t* data, uint64_t fileSize);

    uint8_t* LoadToRAM(FIL* file);
    bool MountKUC(const char* path, uint8_t* workspace, size_t capacity, CSCCSound& sound);

    char _romDatabasePath[ROM_DATABASE_PATH_MAX + 1] = ROM_DATABASE_DEFAULT;
    MountResult _mountResult = MountResult::Unknown;
    CCartridge* _current;
    CCartridge* _prev;
    // No FATFS object here: the single volume is mounted on a file-scope static
    // in FSAdapter.c (_fs_mount). An unused FATFS member cost ~552 bytes of the
    // RAM this build has almost none of -- it was never mounted or referenced.
};