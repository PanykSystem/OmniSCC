#include "OmniSCC.hxx"
#include "KUCCart.hxx"
#include "KUCSectorSave.hxx"
#ifdef PICO_ON_DEVICE
#include "Loader.hxx"
// The active menu survives mount failures. Both objects and allocator overhead
// must fit together until ownership has safely transferred to the cartridge.
static_assert(sizeof(CKUCCartridge) + sizeof(CLoader) + 64 <= 2048,
              "KUC transaction buffers exceed the menu/cartridge heap budget");
#endif
#include "RomStorageBuilder.hxx"
#include "VolumeAccess.h"
extern "C" {
#include "FlashAdapter.h"
}
#include "SHA256.hxx"

#include <cstring>
#include <cstdio>
#include <new>

namespace {
constexpr uint32_t SaveCapacity = KUCSectorSave::Capacity;
// The map is packed16-bit addresses, but the following scratch map must be
// uint32-aligned even when the fixed file has an odd number of FAT sectors.
constexpr size_t SaveMapBytes = (SaveCapacity / 512 * sizeof(uint16_t) + 3) & ~size_t(3);
constexpr size_t IndexBytes = KUCSectorSave::IndexEntries * sizeof(uint16_t);

// Local handles close on every error path. Successful paths explicitly close
// and check results before filesystem ownership is transferred to the game.
struct File {
    FIL handle{};
    bool opened = false;
    FRESULT result = FR_OK;
    bool Open(const char* path, BYTE flags) {
        result = f_open(&handle, path, flags);
        opened = result == FR_OK;
        return opened;
    }
    bool Close() {
        if (!opened) return true;
        opened = false;
        return f_close(&handle) == FR_OK;
    }
    ~File() { if (opened) f_close(&handle); }
};

bool HashROM(FIL& file, uint8_t digest[32])
{
    CSHA256 hash;
    uint8_t buffer[512];
    UINT got;
    bool ok = f_lseek(&file, 0) == FR_OK;
    while (ok && file.fptr < f_size(&file)) {
        ok = f_read(&file, buffer, sizeof(buffer), &got) == FR_OK && got != 0;
        if (ok) hash.Update(buffer, got);
    }
    if (ok) hash.Final(digest);
    return ok && f_lseek(&file, 0) == FR_OK;
}

bool Provision(const char* path, MountResult* failure = nullptr)
{
    // FatFs may report a short successful write when the volume is full.
    // Check whole clusters first, before allocating or filling a temporary file.
    FATFS* fs;
    DWORD freeClusters;
    if (f_getfree(path, &freeClusters, &fs) != FR_OK) return false;
    const uint32_t clusterBytes = uint32_t(fs->csize) * 512;
    const uint32_t required = (SaveCapacity + clusterBytes - 1) / clusterBytes;
    if (freeClusters < required) {
        if (failure) *failure = MountResult::SaveNoSpace;
        return false;
    }
    File file;
    if (!file.Open(path, FA_CREATE_NEW | FA_WRITE)) return false;
    uint8_t blank[512];
    std::memset(blank, 0xff, sizeof(blank));
    bool ok = true;
    for (uint32_t offset = 0; offset < SaveCapacity; offset += sizeof(blank)) {
        UINT written = 0;
        const FRESULT result = f_write(&file.handle, blank, sizeof(blank), &written);
        if (result != FR_OK || written != sizeof(blank)) {
            if (result == FR_OK && failure) *failure = MountResult::SaveNoSpace;
            ok = false;
            break;
        }
    }
    if (ok) ok = f_sync(&file.handle) == FR_OK;
    const bool closed = file.Close();
    if (ok && closed && volume_sync(VOLUME_MENU)) return true;
    // This path was created exclusively by this attempt. Never leave a partial
    // allocation consuming the remaining space, or touch an existing save.
    f_unlink(path);
    volume_sync(VOLUME_MENU);
    return false;
}

bool MapSave(FIL& file, uint8_t* workspace, size_t capacity)
{
    if (f_size(&file) != SaveCapacity || f_lseek(&file, 0) != FR_OK) return false;
    RomStorage mapped;
    if (BuildRomStorage(file, workspace, capacity, mapped, true) != MountResult::Ok) return false;
    // Compress forward in place. The overwritten half always belongs to entries
    // already consumed, leaving the rest of the uint32_t table intact.
    auto* compact = reinterpret_cast<uint16_t*>(workspace);
    for (uint32_t i = 0; i < SaveCapacity / 512; ++i) {
        const uint32_t physical = mapped.pages[i] - XIP_BASE;
        if (physical < FS_BASE || physical >= PICO_FLASH_SIZE_BYTES || physical % 512) return false;
        compact[i] = uint16_t(physical / 512);
    }
    return true;
}

bool Exists(const char* path)
{
    FILINFO info;
    return f_stat(path, &info) == FR_OK;
}

bool RemoveTemporary(const char* path, const uint8_t digest[32])
{
    if (!Exists(path)) return true;
    File file;
    uint8_t header[256];
    UINT got;
    std::memset(header, 0xff, sizeof(header));
    if (!file.Open(path, FA_READ) || f_size(&file.handle) > SaveCapacity
        || f_read(&file.handle, header, sizeof(header), &got) != FR_OK) return false;
    // These suffixes are reserved for our journal preparation. Recognize both
    // complete identities and a NOR-program prefix interrupted by power loss.
    const char magic[] = "OSKUC003";
    for (unsigned i = 0; i < 8; ++i) {
        if ((header[i] & uint8_t(magic[i])) != uint8_t(magic[i])) return false;
    }
    for (unsigned i = 0; i < 32; ++i) {
        if ((header[20 + i] & digest[i]) != digest[i]) return false;
    }
    return file.Close() && f_unlink(path) == FR_OK;
}

bool PrepareSave(const char* path, FIL& rom, const uint8_t digest[32],
                 uint8_t* workspace, size_t capacity, MountResult* failure = nullptr)
{
    char temporary[272], backup[272];
    if (std::snprintf(temporary, sizeof(temporary), "%s.kuc-new", path) >= int(sizeof(temporary))
        || std::snprintf(backup, sizeof(backup), "%s.kuc-bak", path) >= int(sizeof(backup))) return false;
    if (!Exists(path) && Exists(backup)) {
        if (f_rename(backup, path) != FR_OK || !volume_sync(VOLUME_MENU)) return false;
    }
    FILINFO attributes;
    const FRESULT info = f_stat(path, &attributes);
    if (info == FR_OK && (attributes.fattrib & (AM_RDO | AM_DIR))) return false;
    if (info != FR_OK && info != FR_NO_FILE && info != FR_NO_PATH) return false;
    if (!RemoveTemporary(temporary, digest)) return false;

    File old;
    const bool existing = old.Open(path, FA_READ);
    if (!existing && old.result != FR_NO_FILE && old.result != FR_NO_PATH) return false;
    // Two compressed maps and two independent replay indexes coexist only
    // during menu-time compaction. The ROM table is built afterwards.
    const size_t newMapOffset = SaveMapBytes + 2 * IndexBytes;
    if (capacity <= newMapOffset) return false;
    auto* oldIndex = reinterpret_cast<uint16_t*>(workspace + SaveMapBytes);
    auto* newIndex = reinterpret_cast<uint16_t*>(workspace + SaveMapBytes + IndexBytes);
    KUCSaveStorage oldStorage(reinterpret_cast<uint16_t*>(workspace), SaveCapacity);
    KUCSectorSave previous(oldStorage.SectorIO(), oldIndex, KUCSectorSave::IndexEntries);
    bool previousHealthy = false;
    if (existing) {
        if (!MapSave(old.handle, workspace, capacity)) return false;
        const auto result = previous.Open(digest, uint32_t(f_size(&rom)));
        if (result != KUCSaveResult::Ok && result != KUCSaveResult::TornTail) return false;
        previousHealthy = result == KUCSaveResult::Ok;
        // Normal launches append to the existing erased tail. Rewriting the
        // whole allocation at every boot is unnecessary and wears the flash.
        if (result == KUCSaveResult::Ok && previous.RemainingSlots() >= 16) {
            if (!old.Close() || !RemoveTemporary(backup, digest)) return false;
            return f_lseek(&rom, 0) == FR_OK;
        }
    }
    if (!Provision(temporary, failure)) return false;
    File next;
    if (!next.Open(temporary, FA_READ)
        || !MapSave(next.handle, workspace + newMapOffset, capacity - newMapOffset)) return false;
    KUCSaveStorage newStorage(reinterpret_cast<uint16_t*>(workspace + newMapOffset), SaveCapacity);
    KUCSectorSave compacted(newStorage.SectorIO(), newIndex, KUCSectorSave::IndexEntries);
    if (compacted.Create(digest, uint32_t(f_size(&rom))) != KUCSaveResult::Ok) return false;
    KUCSaveResult compactResult = KUCSaveResult::Ok;
    const bool copied = [&]() {
        if (!existing) return true;
        uint8_t page[512], original[512];
        uint32_t eraseStart = 0, eraseBytes = 0;
        auto flushErase = [&]() {
            if (!eraseBytes) return true;
            compactResult = compacted.Erase(eraseStart, eraseBytes);
            const bool ok = compactResult == KUCSaveResult::Ok;
            eraseBytes = 0;
            return ok;
        };
        for (uint32_t address = 0; address < 0x800000; address += sizeof(page)) {
            if (!oldIndex[address >> 9]) {
                if (!flushErase()) return false;
                continue;
            }
            std::memset(original, 0xff, sizeof(original));
            if (address < f_size(&rom)) {
                UINT got;
                if (f_lseek(&rom, address) != FR_OK
                    || f_read(&rom, original, sizeof(original), &got) != FR_OK) return false;
            }
            bool changed = false, allBlank = true;
            for (unsigned i = 0; i < sizeof(page); ++i) {
                page[i] = original[i];
                previous.ReadByte(address + i, page[i]);
                changed |= page[i] != original[i];
                allBlank &= page[i] == 0xff;
            }
            if (!previous.Healthy()) return false;
            if (allBlank && (changed || eraseBytes)) {
                if (!eraseBytes) eraseStart = address;
                eraseBytes += sizeof(page);
                continue;
            }
            if (!flushErase()) return false;
            if (!changed) continue;
            // Each saved payload is a complete512-byte sector. Copying only
            // changed32-byte blocks would lose the original ROM neighbours.
            compactResult = compacted.Program(address, page, sizeof(page));
            if (compactResult != KUCSaveResult::Ok) return false;
        }
        if (!flushErase()) return false;
        return true;
    }();
    if (!copied) {
        // A healthy full file can still serve reads and direct byte programs.
        // Never replace it with a partial compaction or refuse those operations
        // just because the compacted representation needs one more descriptor.
        if (compactResult != KUCSaveResult::Full || !previousHealthy
            || !previous.Healthy() || !old.Close() || !next.Close()) return false;
        return RemoveTemporary(temporary, digest) && volume_sync(VOLUME_MENU)
            && f_lseek(&rom, 0) == FR_OK;
    }
    if (!old.Close() || !next.Close() || !volume_sync(VOLUME_MENU)) return false;
    if (existing) {
        if (!RemoveTemporary(backup, digest) || f_rename(path, backup) != FR_OK
            || !volume_sync(VOLUME_MENU)) return false;
    }
    if (f_rename(temporary, path) != FR_OK || !volume_sync(VOLUME_MENU)) return false;
    // Backup is only a transaction guard during replacement, not a permanent
    // second save. The new file has been synced before retiring the old one.
    if (!RemoveTemporary(backup, digest) || !volume_sync(VOLUME_MENU)) return false;
    return f_lseek(&rom, 0) == FR_OK;
}
} // namespace

bool COmniSCC::MountKUC(const char* path, uint8_t* workspace, size_t capacity, CSCCSound& sound)
{
    _mountResult = MountResult::FileRead;
    extern uint8_t __kuc_runtime_start__[], __kuc_runtime_end__[], __kuc_runtime_source__[];
    std::memcpy(__kuc_runtime_start__, __kuc_runtime_source__,
                size_t(__kuc_runtime_end__ - __kuc_runtime_start__));
    if (!KUCSaveStorage::SupportedChip((_kucOptions & KUC_OPTION_ALLOW_UNKNOWN_FLASH) != 0)) return false;
    File rom;
    if (!rom.Open(path, FA_READ)) {
        _mountResult = MountFileMissing(rom.result) ? MountResult::RomMissing : MountResult::FileRead;
        return false;
    }
    const uint64_t size = f_size(&rom.handle);
    if (!size || size > 0x800000) { _mountResult = MountResult::RomSize; return false; }
    char savePath[256];
    if (std::strlen(path) >= sizeof(savePath)) return false;
    std::strcpy(savePath, path);
    char* extension = std::strrchr(savePath, '.');
    const char* slash = std::strrchr(savePath, '/');
    if (!extension || (slash && extension < slash)) extension = savePath + std::strlen(savePath);
    if (size_t(extension - savePath) + 5 > sizeof(savePath)) return false;
    std::strcpy(extension, ".sav");
    uint8_t digest[32];
    if (!HashROM(rom.handle, digest)) return false;
    _mountResult = MountResult::SavePrepare;
    if (!PrepareSave(savePath, rom.handle, digest, workspace, capacity, &_mountResult)) return false;
    _mountResult = MountResult::FileRead;
    RomStorage storage;
    if (BuildRomStorage(rom.handle, workspace, capacity, storage, true) != MountResult::Ok) return false;
    const size_t romStable = size_t(storage.pageCount) * 4;
    if (romStable + SaveMapBytes + IndexBytes > capacity) return false;
    File saveFile;
    if (!saveFile.Open(savePath, FA_READ)
        || !MapSave(saveFile.handle, workspace + romStable, capacity - romStable)) return false;
    auto* pages = reinterpret_cast<uint16_t*>(workspace + romStable);
    auto* index = reinterpret_cast<uint16_t*>(workspace + romStable + SaveMapBytes);
    if (!rom.Close() || !saveFile.Close() || !volume_sync(VOLUME_MENU)) return false;
    // The retired preloader is no longer executing. Release it before the
    // larger KUC object is allocated; keeping two loaders plus KUC would
    // exceed the fixed 2 KiB heap. The active menu survives every failure.
    if (_prev) { delete _prev; _prev = nullptr; }
    auto* candidate = new(std::nothrow) CKUCCartridge(*this, sound, storage, pages, SaveCapacity, index, digest);
    if (!candidate) return false;
    if (!candidate->Ready() || !volume_handover(VOLUME_MENU, VOLUME_ROM)) {
        delete candidate;
        return false;
    }
    if (_prev) delete _prev;
    _prev = _current;
    _current = candidate;
    _mountResult = MountResult::Ok;
    return true;
}
