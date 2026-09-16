/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 */

#pragma once

#include <stdint.h>

extern "C" {
#include "ff.h"
}

#define DSK_SECTOR_SIZE 512u

// One byte more than DISK_BASIC_PATH_MAX (DiskCart.hxx) for the terminator --
// the longest path CMD_BASIC_MOUNT will accept has to fit here with room for
// the NUL, or the last legal path is the one that gets silently truncated.
// test_constants.py checks the two stay in step.
#define DISK_PATH_MAX 65

/**
 * A .dsk image mounted from the internal FAT volume.
 *
 * There is no geometry here on purpose. A .dsk is a flat array of 512-byte
 * sectors -- openMSX models it the same way, deriving sides and tracks nowhere
 * except when it creates a new image. Real geometry reaches the MSX through the
 * BPB inside the image's own boot sector, which GETDPB reads back out.
 */
class CDiskImage {
public:
    CDiskImage();
    ~CDiskImage();

    // Open `path` and prepare it for sector access. Replaces any image already
    // mounted and raises the disk-changed flag.
    bool Mount(const char* path);

    // Close the mounted image, if any, and reset the sector count. Ejecting
    // media is itself a change, so this raises the disk-changed flag whenever
    // it actually transitions from mounted to unmounted. Calling it when
    // nothing is mounted is a safe no-op and leaves the flag untouched.
    void Unmount();

    bool SameFile(const FIL& file) const {
        return _mounted && _file.obj.fs == file.obj.fs &&
               _file.obj.sclust == file.obj.sclust;
    }

    bool Mounted() const { return _mounted; }
    uint32_t SectorCount() const { return _sectors; }

    // True when Read serves from the direct map rather than falling back to
    // FatFs.
    bool Mapped() const { return _mapped; }

    // Provide the storage the direct LBA -> flash-sector map is built into
    // (owned by COmniSCC, carved from car_cache). Call before Mount; without it,
    // or when an image does not fit, Read falls back to FatFs. `map` holds one
    // FS sector per image sector; `clmtScratch` is a build-time fragment table.
    void SetDirectMap(uint16_t* map, uint32_t mapCapSectors,
                      DWORD* clmtScratch, uint32_t clmtDwords);

    // Read `count` consecutive sectors starting at `lba` into `dst`.
    bool Read(uint32_t lba, uint8_t count, uint8_t* dst);

    // Write `count` consecutive sectors starting at `lba` from `src`, in place.
    // Sector contents are the only thing that changes: the file keeps its size
    // and its cluster chain, which is what lets the mapped path write straight
    // to flash and leaves the direct map valid afterwards.
    //
    // The write reaches flash but the filesystem's own sector map does not --
    // call Sync() once the caller has finished writing, or a power cycle loses
    // both the new data and the sectors it replaced.
    bool Write(uint32_t lba, uint8_t count, const uint8_t* src);

    // Persist what Write left in RAM: the flash filesystem's sector map on the
    // mapped path, FatFs's own metadata on the fallback path (which reaches the
    // same map through CTRL_SYNC).
    bool Sync();

    // Write a fresh MSX filesystem over the image: boot sector, both FATs and
    // an empty root directory. The data area is left as it was -- a format on a
    // real floppy does not scrub it either, and there is no reason to spend
    // hundreds of erase cycles making deleted files unrecoverable.
    //
    // `scratch` is one sector's worth of workspace the caller owns; the
    // exchange buffer serves, since nothing reads it while a command runs.
    //
    // Returns false for a read-only image, a write failure, or a size whose BPB
    // this project will not invent -- see DskGeometryFor. A failure part way
    // leaves a half-written filesystem, which is what a real drive does with a
    // failed format too; the image still mounts, and formatting again is the
    // way out.
    bool Format(uint8_t* scratch);

    // True when the image cannot be written: the file is read-only, which is
    // this cartridge's equivalent of a write-protect notch. Reported to the MSX
    // through CMD_DISK_STATUS, so Disk BASIC refuses a SAVE rather than
    // failing it halfway.
    bool ReadOnly() const { return _readOnly; }

    // The path Mount() was given, or "" when nothing is mounted. Reported by
    // CMD_BASIC_INFO so CALL DISK can name the image without FatFs's help --
    // FIL keeps no name of its own.
    const char* Path() const { return _path; }

    // Read the disk-changed flag and clear it, so a mount is reported once.
    bool TakeChangedFlag();

private:
    // Direct map: image sector -> FS (flash) sector. When built at Mount, Read
    // serves sectors straight from flash with no FatFs call, so a fragmented or
    // large image cannot drag a linear cluster-chain seek into the /WAIT-held
    // read handler (which stalls the Z80 and starves MSX DRAM refresh). Storage
    // is owned by COmniSCC; see SetDirectMap.
    bool buildDirectMap();
    bool verifyDirectMap();

    FIL _file;
    uint16_t* _map;             // image sector -> FS sector, _sectors entries
    uint32_t _mapCap;           // capacity of _map in sectors
    DWORD* _clmtScratch;        // build-time fragment table (FatFs CLMT)
    uint32_t _clmtCap;          // its size in DWORDs
    uint32_t _sectors;
    bool _mounted;
    bool _changed;
    bool _mapped;               // _map covers the whole image
    bool _readOnly;             // opened for reading only: a protected disk

    // The path Mount() opened, for CMD_BASIC_INFO to report. FatFs keeps no
    // name in FIL, and the cartridge has nowhere else to remember it.
    char _path[DISK_PATH_MAX];
};
