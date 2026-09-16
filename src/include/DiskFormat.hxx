/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 */

#pragma once

#include <stdint.h>
#include <string.h>

/**
 * The boot sector a format writes, and the geometry that follows from it.
 *
 * Header-only and free of both the Pico SDK and FatFs on purpose: the
 * cartridge (CDiskImage::Format), the openMSX device and the host test all
 * build the same bytes from the same code rather than from three copies of the
 * same table. A drift here would be a disk the vendor Disk ROM reads with the
 * wrong geometry, which corrupts on the first write rather than failing to
 * mount.
 *
 * There is no geometry anywhere else in this project -- an image is a flat run
 * of 512-byte sectors, and real geometry reaches the MSX through the BPB inside
 * the image's own boot sector, which the vendor GETDPB reads back out. This is
 * the one place that writes that BPB, which is why formatting is limited to the
 * sizes whose BPB is standard.
 */

#define DSK_FORMAT_SECTOR_SIZE 512u

struct DskGeometry {
    uint16_t totalSectors;      // whole image
    uint8_t  sectorsPerCluster;
    uint8_t  reservedSectors;   // the boot sector itself
    uint8_t  fatCount;
    uint16_t rootEntries;
    uint8_t  media;             // media descriptor byte, also FAT[0]
    uint8_t  sectorsPerFat;
    uint8_t  sectorsPerTrack;
    uint8_t  heads;
};

/**
 * The two layouts a `.dsk` overwhelmingly means, and the only two whose BPB is
 * standard enough to write blind. Returns false for any other size: an image
 * that cannot be formatted still mounts and still reads, because nothing here
 * is consulted unless the MSX asks for a format.
 */
static inline bool DskGeometryFor(uint32_t sectors, DskGeometry& g)
{
    if (sectors == 720) {           // 360 KB, single sided
        g = { 720, 2, 1, 2, 112, 0xF8, 2, 9, 1 };
        return true;
    }
    if (sectors == 1440) {          // 720 KB, double sided
        g = { 1440, 2, 1, 2, 112, 0xF9, 3, 9, 2 };
        return true;
    }
    return false;
}

/** First sector of the root directory, and how many sectors it occupies. */
static inline uint16_t DskRootDirStart(const DskGeometry& g)
{
    return (uint16_t)(g.reservedSectors + (uint16_t)g.fatCount * g.sectorsPerFat);
}

static inline uint16_t DskRootDirSectors(const DskGeometry& g)
{
    return (uint16_t)((g.rootEntries * 32u + DSK_FORMAT_SECTOR_SIZE - 1) /
                      DSK_FORMAT_SECTOR_SIZE);
}

/**
 * Build the 512-byte boot sector for `g` into `dst`.
 *
 * The code area is filled with 0xC9 (ret) rather than left zero. The MSX boot
 * path loads this sector to 0xC000 and calls 0xC01E, so a zeroed area would be
 * 480 NOPs followed by whatever RAM holds next; a wall of `ret` makes any entry
 * into it a clean "this is not a system disk" return to the BIOS, which then
 * carries on to BASIC. The three bytes at the front are the FAT world's
 * `EB FE 90` marker and are never executed on an MSX -- the BIOS enters at
 * 0x1E, not at 0.
 */
static inline void DskBuildBootSector(uint8_t* dst, const DskGeometry& g)
{
    memset(dst, 0xC9, DSK_FORMAT_SECTOR_SIZE);

    dst[0x00] = 0xEB;
    dst[0x01] = 0xFE;
    dst[0x02] = 0x90;
    memcpy(&dst[0x03], "OMNISCC ", 8);

    dst[0x0B] = (uint8_t)(DSK_FORMAT_SECTOR_SIZE & 0xFF);
    dst[0x0C] = (uint8_t)(DSK_FORMAT_SECTOR_SIZE >> 8);
    dst[0x0D] = g.sectorsPerCluster;
    dst[0x0E] = g.reservedSectors;
    dst[0x0F] = 0;
    dst[0x10] = g.fatCount;
    dst[0x11] = (uint8_t)(g.rootEntries & 0xFF);
    dst[0x12] = (uint8_t)(g.rootEntries >> 8);
    dst[0x13] = (uint8_t)(g.totalSectors & 0xFF);
    dst[0x14] = (uint8_t)(g.totalSectors >> 8);
    dst[0x15] = g.media;
    dst[0x16] = g.sectorsPerFat;
    dst[0x17] = 0;
    dst[0x18] = g.sectorsPerTrack;
    dst[0x19] = 0;
    dst[0x1A] = g.heads;
    dst[0x1B] = 0;
    dst[0x1C] = 0;                  // hidden sectors
    dst[0x1D] = 0;
}

/**
 * Build the first sector of a FAT: the media descriptor and two 0xFF bytes,
 * which is FAT12's way of saying "clusters 0 and 1 are reserved", then empty.
 * Every other FAT sector, and every root-directory sector, is all zero.
 */
static inline void DskBuildFirstFatSector(uint8_t* dst, const DskGeometry& g)
{
    memset(dst, 0, DSK_FORMAT_SECTOR_SIZE);
    dst[0] = g.media;
    dst[1] = 0xFF;
    dst[2] = 0xFF;
}
