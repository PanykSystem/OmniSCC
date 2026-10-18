#include "RomPreparation.hxx"
#include "RomDatabase.hxx"
#include "SHA1.hxx"
#include "VolumeAccess.h"
extern "C" {
#include "ff.h"
}
#include <cstring>
#include <cstdio>

namespace {
bool Join(const char* prefix, const char* name, char* path, size_t capacity)
{
    const int size = std::snprintf(path, capacity, "%s%s", prefix, name);
    return size >= 0 && size_t(size) < capacity;
}

MountResult CanonicalPath(char* path, size_t capacity)
{
    if (std::strlen(path) >= 256) return MountResult::RomMissing;
    char directory[256];
    const char* slash = std::strrchr(path, '/');
    const size_t length = slash ? size_t(slash - path + 1) : 0;
    if (length >= sizeof(directory)) return MountResult::RomMissing;
    std::memcpy(directory, path, length); directory[length] = 0;
    FILINFO info;
    FRESULT result = f_stat(path, &info);
    if (result == FR_OK) {
        if (info.fattrib & AM_DIR) return MountResult::RomMissing;
        return Join(directory, info.fname, path, capacity) ? MountResult::Ok : MountResult::RomMissing;
    }
    if (!MountFileMissing(result)) return MountResult::FileRead;
    // Old last=/favN=/8.3 references are not rewritten. Only a unique tagged
    // successor in the SAME directory may stand in for a missing bare name.
    const char* requested = slash ? slash + 1 : path;
    if (RomMapperTag(requested)) return MountResult::RomMissing;
    char wanted[256];
    std::strcpy(wanted, requested);
    DIR dir;
    result = f_opendir(&dir, *directory ? directory : ".");
    if (result != FR_OK) return MountFileMissing(result) ? MountResult::RomMissing : MountResult::FileRead;
    unsigned matches = 0;
    char candidate[256] = {};
    for (;;) {
        result = f_readdir(&dir, &info);
        if (result != FR_OK || !*info.fname) break;
        if (!(info.fattrib & AM_DIR) && RomMapperTag(info.fname)
            && RomSameUntaggedName(wanted, info.fname)) {
            ++matches;
            if (!Join(directory, info.fname, candidate, sizeof(candidate))) matches = 2;
        }
    }
    const FRESULT closed = f_closedir(&dir);
    if (result != FR_OK || closed != FR_OK) return MountResult::FileRead;
    if (matches != 1 || std::strlen(candidate) >= capacity) return MountResult::RomMissing;
    std::strcpy(path, candidate);
    return MountResult::Ok;
}
}

namespace {
struct DatabaseFile {
    FIL file{};
    bool opened = false;
    ~DatabaseFile() { if (opened) f_close(&file); }
    static bool Read(void* context, uint32_t offset, uint8_t* bytes, size_t size) {
        auto* f = static_cast<FIL*>(context);
        UINT got;
        return f_lseek(f, offset) == FR_OK && f_read(f, bytes, UINT(size), &got) == FR_OK && got == size;
    }
};
}

MountResult PrepareRomPath(char* path, size_t capacity, const char*& mapper,
                          const char* (*lookup)(const uint8_t*), const char* databasePath)
{
    mapper = nullptr;
    if (!path || !capacity || !std::memchr(path, 0, capacity)) return MountResult::RomMissing;
    if (!volume_sync(VOLUME_MENU)) return MountResult::FileRead;
    const MountResult resolved = CanonicalPath(path, capacity);
    if (resolved != MountResult::Ok) return resolved;
    mapper = RomMapperTag(path);
    if (mapper) return MountResult::Ok;
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) return MountResult::FileRead;
    if (!f_size(&file) || f_size(&file) > 0x800000) {
        f_close(&file); return MountResult::RomSize;
    }
    CSHA1 hash;
    uint8_t buffer[512], digest[20];
    UINT got;
    bool ok = f_read(&file, buffer, sizeof(buffer), &got) == FR_OK && got != 0;
    if (ok && got >= 24 && !std::memcmp(buffer + 16, "OmniSCC", 8)) {
        return f_close(&file) == FR_OK ? MountResult::Ok : MountResult::FileRead;
    }
    DatabaseFile databaseFile;
    RomDatabase database;
    bool validDatabase = lookup != nullptr;
    if (!lookup && databasePath && *databasePath
        && f_open(&databaseFile.file, databasePath, FA_READ) == FR_OK) {
        databaseFile.opened = true;
        if (f_size(&databaseFile.file) <= UINT32_MAX) {
            validDatabase = database.Open({&databaseFile.file, uint32_t(f_size(&databaseFile.file)), DatabaseFile::Read});
        }
    }
    if (!ok || !validDatabase) {
        const bool closed = f_close(&file) == FR_OK;
        return ok && closed ? MountResult::Ok : MountResult::FileRead;
    }
    hash.Update(buffer, got);
    while (ok && file.fptr < f_size(&file)) {
        ok = f_read(&file, buffer, sizeof(buffer), &got) == FR_OK && got != 0;
        if (ok) hash.Update(buffer, got);
    }
    ok = f_close(&file) == FR_OK && ok;
    if (!ok) return MountResult::FileRead;
    hash.Final(digest);
    if (lookup) mapper = lookup(digest);
    else {
        char name[32];
        if (database.Lookup(digest, name)) {
            mapper = RomKnownMapperName(name);
            if (!mapper) mapper = "Unsupported";
        }
    }
    if (databaseFile.opened) {
        f_close(&databaseFile.file); databaseFile.opened = false;
    }
    if (!mapper || !std::strcmp(mapper, "Unsupported")) return MountResult::Ok;
    char tagged[256];
    if (RomTaggedPath(path, mapper, tagged, sizeof(tagged)) && std::strlen(tagged) < capacity
        && f_rename(path, tagged) == FR_OK) {
        std::strcpy(path, tagged);
        // Metadata writes may relocate sectors. Build final ROM maps afterwards.
        if (!volume_sync(VOLUME_MENU)) return MountResult::FileRead;
    }
    return MountResult::Ok;
}
