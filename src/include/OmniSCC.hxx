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
#include "DiskImage.hxx"
#include "DiskFiles.hxx"

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
        CT_LLast
    };

    COmniSCC();
    ~COmniSCC();

    bool Start();
    bool Mount(const char* fileName);
    bool MountLoader();
    bool MountSCC();

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

    bool Mounted() { return _current != 0; };

private:

    static constexpr const char* _loaderROMName = "loader.rom";
    static constexpr const char* _diskROMName = "disk.rom";

    CDiskImage _diskImage;          // drive A
    CDiskImage _diskImage2;         // drive B, empty unless disk2= named one
    // COmniSCC lives on core 0's 16 KiB stack. Keep the two transfer handles
    // here so the heap needs only the small cartridge object during boot.
    CDiskFiles _diskFiles;

    CartType DetectCartType(uint8_t* data, uint64_t fileSize);

    // Copies the loader/menu ROM into its own buffer and makes it current.
    // Keeps the running menu alive when a later Mount() overwrites car_cache.
    bool MountLoaderROM(const uint8_t* data, uint64_t fileSize);

    uint8_t* LoadToRAM(FIL* file);
    uint8_t* LoadToFlash(FIL* file);

    CCartridge* _current;
    CCartridge* _prev;
    // No FATFS object here: the single volume is mounted on a file-scope static
    // in FSAdapter.c (_fs_mount). An unused FATFS member cost ~552 bytes of the
    // RAM this build has almost none of -- it was never mounted or referenced.
};