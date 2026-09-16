/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 */

#include "DiskImage.hxx"
#include "DiskFormat.hxx"
#include <string.h>

// Read one 512-byte volume (flash) sector straight from the flash filesystem,
// bypassing FatFs. Declared here to keep the C header's SDK includes out of
// this C++ unit.
extern "C" void _flash_fs_read(uint16_t fat_sector, void* buffer);

// The write side of the same bypass, plus the filesystem's map flush. A write
// only ever replaces the contents of a sector that already exists, so the
// remapping this does underneath (every write lands on a fresh physical page
// for wear levelling) stays invisible to the direct map, which holds volume
// sector numbers rather than physical ones.
extern "C" bool _flash_fs_write(uint16_t fat_sector, const void* buffer);
extern "C" void _fs_sync();

CDiskImage::CDiskImage()
    : _map(nullptr), _mapCap(0), _clmtScratch(nullptr), _clmtCap(0),
      _sectors(0), _mounted(false), _changed(false), _mapped(false),
      _readOnly(false)
{
    _path[0] = '\0';
}

void CDiskImage::SetDirectMap(uint16_t* map, uint32_t mapCapSectors,
                              DWORD* clmtScratch, uint32_t clmtDwords)
{
    _map = map;
    _mapCap = mapCapSectors;
    _clmtScratch = clmtScratch;
    _clmtCap = clmtDwords;
}

CDiskImage::~CDiskImage()
{
    Unmount();
}

void CDiskImage::Unmount()
{
    if (_mounted) {
        f_close(&_file);
        _mounted = false;
        // The drive just went from occupied to empty -- that is a media
        // change in its own right, so surface it even when the caller never
        // gets as far as a subsequent successful Mount().
        _changed = true;
    }
    _sectors = 0;

    // Nothing is mounted, so CMD_BASIC_INFO has nothing to report -- and
    // Mount() calls this first, so a failed open leaves this "" rather than
    // the path that did not actually load.
    _path[0] = '\0';
}

bool CDiskImage::Mount(const char* path)
{
    Unmount();

    // Prefer read/write. A file the volume will not hand over for writing is a
    // write-protected disk, not a failed mount -- the same thing the notch on a
    // real floppy does -- so fall back to read-only and tell the MSX through
    // CMD_DISK_STATUS. Setting the read-only attribute over USB is therefore
    // how a user protects an image.
    _readOnly = false;
    if (f_open(&_file, path, FA_READ | FA_WRITE) != FR_OK) {
        if (f_open(&_file, path, FA_READ) != FR_OK)
            return false;
        _readOnly = true;
    }

    const FSIZE_t size = f_size(&_file);

    // Any whole number of sectors is a valid image; sizes are not whitelisted.
    if (size == 0 || (size % DSK_SECTOR_SIZE) != 0) {
        f_close(&_file);
        return false;
    }

    _sectors = (uint32_t)(size / DSK_SECTOR_SIZE);

    // Prefer a direct LBA -> flash-sector map: reads then bypass FatFs entirely,
    // so a fragmented or large image cannot pull a linear cluster-chain seek into
    // the /WAIT-held read handler and starve MSX DRAM refresh. If the image is
    // too large for the map storage or too fragmented for the scratch, fall back
    // to FatFs reads with a best-effort fast-seek table.
    _mapped = buildDirectMap();
    if (!_mapped && _clmtScratch && _clmtCap) {
        // Fall back to FatFs reads with a fast-seek table. It is built in the
        // same scratch the direct map would have used -- that buffer is idle
        // from here on, and a second table of our own would be 256 bytes of a
        // full SRAM region spent on the path that is already the slow one.
        _clmtScratch[0] = _clmtCap;
        _file.cltbl = _clmtScratch;
        if (f_lseek(&_file, CREATE_LINKMAP) != FR_OK)
            _file.cltbl = 0;
    }

    // Remembered for CMD_BASIC_INFO -- FatFs's FIL keeps no name, so this is
    // the only place left to ask. Truncated rather than rejected: the mount
    // already succeeded above, and CMD_BASIC_MOUNT is the one that caps what
    // it will hand in at DISK_BASIC_PATH_MAX (DiskCart.hxx), one shorter than
    // this buffer so it always fits with room for the terminator.
    strncpy(_path, path, sizeof(_path) - 1);
    _path[sizeof(_path) - 1] = '\0';

    _mounted = true;
    _changed = true;
    return true;
}

bool CDiskImage::Read(uint32_t lba, uint8_t count, uint8_t* dst)
{
    if (!_mounted || count == 0)
        return false;

    // Reject the whole request rather than returning a short read: DSKIO has no
    // way to say "the first three sectors are good and the fourth is not".
    // Written as subtraction, not `lba + count > _sectors`, so the guard
    // cannot be bypassed by lba+count wrapping around 2^32 for an lba near
    // UINT32_MAX; the short-circuit on the first clause guarantees the
    // subtraction below never underflows.
    if (lba > _sectors || count > _sectors - lba)
        return false;

    if (_mapped) {
        // No FatFs, no seek: one flash-map lookup and read per sector, so the
        // /WAIT-held handler runs for a short, bounded time regardless of how
        // large or fragmented the image is.
        for (uint8_t i = 0; i < count; i++)
            _flash_fs_read(_map[lba + i], dst + (uint32_t)i * DSK_SECTOR_SIZE);
        return true;
    }

    // Fallback: image too large for the map or too fragmented for the scratch.
    if (f_lseek(&_file, (FSIZE_t)lba * DSK_SECTOR_SIZE) != FR_OK)
        return false;

    UINT want = (UINT)count * DSK_SECTOR_SIZE;
    UINT got = 0;
    if (f_read(&_file, dst, want, &got) != FR_OK || got != want)
        return false;

    return true;
}

bool CDiskImage::Write(uint32_t lba, uint8_t count, const uint8_t* src)
{
    if (!_mounted || _readOnly || count == 0)
        return false;

    // Same guard as Read, written the same way and for the same reason: DSKIO
    // cannot report a partly valid range, and the subtraction cannot underflow
    // because the first clause short-circuits.
    if (lba > _sectors || count > _sectors - lba)
        return false;

    if (_mapped) {
        // Straight to flash, one volume sector at a time, bypassing FatFs the
        // way Read does. Nothing about the file changes -- same size, same
        // clusters -- so the map stays valid and FatFs has nothing to learn.
        //
        // A failure part way leaves the sectors before it written. That is what
        // a real drive does with a bad write too, and DSKIO reports it the same
        // way: the count of sectors that made it, and an error.
        for (uint8_t i = 0; i < count; i++) {
            if (!_flash_fs_write(_map[lba + i],
                                 src + (uint32_t)i * DSK_SECTOR_SIZE))
                return false;
        }
        return true;
    }

    if (f_lseek(&_file, (FSIZE_t)lba * DSK_SECTOR_SIZE) != FR_OK)
        return false;

    UINT want = (UINT)count * DSK_SECTOR_SIZE;
    UINT put = 0;
    return f_write(&_file, src, want, &put) == FR_OK && put == want;
}

bool CDiskImage::Sync()
{
    if (!_mounted)
        return false;

    if (_mapped) {
        // The data pages are already programmed; what is still only in RAM is
        // the map that says where they are, and the freed state of the pages
        // they replaced. Losing that loses the write and the old contents both.
        _fs_sync();
        return true;
    }

    // f_sync writes FatFs's own metadata and issues CTRL_SYNC, which lands in
    // the same _fs_sync.
    return f_sync(&_file) == FR_OK;
}

bool CDiskImage::Format(uint8_t* scratch)
{
    if (!_mounted || _readOnly || !scratch)
        return false;

    DskGeometry g;
    if (!DskGeometryFor(_sectors, g))
        return false;

    DskBuildBootSector(scratch, g);
    if (!Write(0, 1, scratch))
        return false;

    // Both FATs: the first sector carries the media descriptor and the two
    // reserved cluster entries, the rest is empty.
    for (uint8_t f = 0; f < g.fatCount; f++) {
        const uint16_t base =
            (uint16_t)(g.reservedSectors + (uint16_t)f * g.sectorsPerFat);
        DskBuildFirstFatSector(scratch, g);
        if (!Write(base, 1, scratch))
            return false;
        memset(scratch, 0, DSK_FORMAT_SECTOR_SIZE);
        for (uint8_t s = 1; s < g.sectorsPerFat; s++) {
            if (!Write((uint16_t)(base + s), 1, scratch))
                return false;
        }
    }

    // An empty root directory is a run of zero sectors: a zero first byte is
    // "this entry has never been used", which ends the directory scan.
    memset(scratch, 0, DSK_FORMAT_SECTOR_SIZE);
    const uint16_t rootStart = DskRootDirStart(g);
    const uint16_t rootLen = DskRootDirSectors(g);
    for (uint16_t s = 0; s < rootLen; s++) {
        if (!Write((uint32_t)rootStart + s, 1, scratch))
            return false;
    }

    if (!Sync())
        return false;

    // The disk the MSX had is gone. Raising the changed flag is what makes the
    // next DSKCHG report a media change, which is what sends MSX-DOS or Disk
    // BASIC back to GETDPB for the geometry this format just wrote.
    _changed = true;
    return true;
}

// Build the image-sector -> FS-sector map from the file's cluster chain, so
// reads never touch FatFs. Returns false -- and Mount then falls back to FatFs
// reads -- whenever the image will not fit the map, the fragment table overflows
// the scratch, or the map fails its read-back self-check.
bool CDiskImage::buildDirectMap()
{
    if (!_map || !_clmtScratch || _sectors == 0 || _sectors > _mapCap)
        return false;

    // Build the fast-seek fragment table into the (large) scratch. If the file
    // is fragmented past what the scratch holds, f_lseek fails and we fall back.
    _clmtScratch[0] = _clmtCap;
    _file.cltbl = _clmtScratch;
    if (f_lseek(&_file, CREATE_LINKMAP) != FR_OK) {
        _file.cltbl = 0;
        return false;
    }
    _file.cltbl = 0;                    // done: reads will not use FatFs at all

    FATFS* fs = _file.obj.fs;
    if (!fs)
        return false;

    // Sector size is fixed at 512 in this build (FF_MIN_SS == FF_MAX_SS ==
    // DSK_SECTOR_SIZE), so there is no fs->ssize member to consult; the
    // read-back check below still catches any layout surprise.
    const uint32_t csize = fs->csize;                 // sectors per cluster
    const uint32_t database = (uint32_t)fs->database; // first data sector

    // The table (one item in from the size) is {cluster count, start cluster}
    // fragments terminated by a zero count -- the same shape clmt_clust walks.
    // Expand it into one FS sector per image sector; FatFs volume sectors are
    // exactly the flash filesystem's fat_sector index (see FSAdapter _fs_read).
    const DWORD* frag = _clmtScratch + 1;
    uint32_t img = 0;
    for (;;) {
        const uint32_t ncl = (uint32_t)*frag++;
        if (ncl == 0)
            break;
        const uint32_t scl = (uint32_t)*frag++;
        for (uint32_t c = 0; c < ncl && img < _sectors; c++) {
            const uint32_t first = database + (scl + c - 2u) * csize;
            for (uint32_t s = 0; s < csize && img < _sectors; s++)
                _map[img++] = (uint16_t)(first + s);
        }
        if (img >= _sectors)
            break;
    }
    if (img < _sectors)                 // fragments did not cover the file
        return false;

    return verifyDirectMap();
}

// Read a few image sectors both ways and require them to match, so a wrong
// mapping can never serve corrupt sectors silently -- on any mismatch the map
// is rejected and Mount falls back to FatFs. Runs only at mount.
bool CDiskImage::verifyDirectMap()
{
    const uint32_t probes[3] = { 0u, _sectors / 2u, _sectors - 1u };
    uint8_t viaMap[DSK_SECTOR_SIZE];
    uint8_t viaFs[DSK_SECTOR_SIZE];
    for (int i = 0; i < 3; i++) {
        const uint32_t lba = probes[i];
        _flash_fs_read(_map[lba], viaMap);
        if (f_lseek(&_file, (FSIZE_t)lba * DSK_SECTOR_SIZE) != FR_OK)
            return false;
        UINT got = 0;
        if (f_read(&_file, viaFs, DSK_SECTOR_SIZE, &got) != FR_OK ||
            got != DSK_SECTOR_SIZE)
            return false;
        if (memcmp(viaMap, viaFs, DSK_SECTOR_SIZE) != 0)
            return false;
    }
    return true;
}

bool CDiskImage::TakeChangedFlag()
{
    const bool was = _changed;
    _changed = false;
    return was;
}
