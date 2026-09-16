#include "DiskFiles.hxx"
#include <cstring>

uint8_t CDiskFiles::Status(FRESULT result)
{
    switch (result) {
    case FR_OK: return DFILE_OK;
    case FR_NO_FILE: case FR_NO_PATH: return DFILE_NOFILE;
    case FR_EXIST: return DFILE_EXISTS;
    case FR_WRITE_PROTECTED: case FR_DENIED: return DFILE_PROTECTED;
    case FR_INVALID_NAME: case FR_INVALID_PARAMETER: return DFILE_INVALID;
    default: return DFILE_IO;
    }
}

void CDiskFiles::Abort()
{
    Finish(DFILE_IO);
}

uint8_t CDiskFiles::Finish(uint8_t status)
{
    if (_sourceOpen) {
        if (f_close(&_source) != FR_OK && status == DFILE_OK) status = DFILE_IO;
        _sourceOpen = false;
    }
    if (_destinationOpen) {
        if (f_close(&_destination) != FR_OK && status == DFILE_OK) status = DFILE_IO;
        _destinationOpen = false;
        // CREATE_NEW guarantees this is our file, never a pre-existing image.
        // Media failure may also prevent cleanup; report failure either way.
        if (status != DFILE_OK) f_unlink(_path);
    }
    return status;
}

uint8_t CDiskFiles::Delete(const char* path)
{
    Abort();
    if (!DskFilePathValid(path)) return DFILE_INVALID;
    FILINFO info;
    FRESULT r = f_stat(path, &info);
    if (r != FR_OK) return Status(r);
    if (info.fattrib & AM_DIR) return DFILE_INVALID;
    if (info.fattrib & AM_RDO) return DFILE_PROTECTED;
    FIL file{};
    r = f_open(&file, path, FA_READ);
    if (r != FR_OK) return Status(r);
    // Identity is the volume and first cluster, not the spelling of the path:
    // long name, short alias, case and leading slash must all be protected.
    const bool mounted = _a.SameFile(file) || _b.SameFile(file);
    r = f_close(&file);
    if (mounted) return DFILE_INUSE;
    if (r != FR_OK) return Status(r);
    return Status(f_unlink(path));
}

uint8_t CDiskFiles::OpenDestination(const char* path)
{
    FRESULT r = f_open(&_destination, path, FA_WRITE | FA_CREATE_NEW);
    if (r != FR_OK) return Status(r);
    _destinationOpen = true;
    std::strcpy(_path, path); // validated to fit before opening
    _sector = 0;
    return DFILE_MORE;
}

uint8_t CDiskFiles::Copy(const char* source, const char* destination)
{
    Abort();
    if (!DskFilePathValid(source) || !DskFilePathValid(destination))
        return DFILE_INVALID;
    // Also persist direct-map writes. The DOS client flushes its own buffers
    // before issuing this command; BASIC callers must close their open files.
    if ((_a.Mounted() && !_a.Sync()) || (_b.Mounted() && !_b.Sync()))
        return DFILE_IO;
    FILINFO info;
    FRESULT r = f_stat(source, &info);
    if (r != FR_OK) return Status(r);
    if ((info.fattrib & AM_DIR) || !info.fsize || info.fsize % 512 ||
        info.fsize / 512 > UINT32_MAX) return DFILE_INVALID;
    r = f_open(&_source, source, FA_READ);
    if (r != FR_OK) return Status(r);
    _sourceOpen = true;
    _sectors = static_cast<uint32_t>(info.fsize / 512);
    uint8_t result = OpenDestination(destination);
    return result == DFILE_MORE ? result : Finish(result);
}

uint8_t CDiskFiles::Create(const char* path, uint16_t sizeKiB)
{
    Abort();
    if (!DskFilePathValid(path) ||
        !DskGeometryFor(uint32_t(sizeKiB) * 2, _geometry)) return DFILE_INVALID;
    _sectors = _geometry.totalSectors;
    return OpenDestination(path);
}

uint8_t CDiskFiles::Step(uint8_t* scratch)
{
    if (!_destinationOpen) return DFILE_INVALID;
    UINT count = 0;
    if (_sourceOpen) {
        if (f_read(&_source, scratch, 512, &count) != FR_OK || count != 512)
            return Finish(DFILE_IO);
    } else if (_sector == 0) {
        DskBuildBootSector(scratch, _geometry);
    } else if (_sector == _geometry.reservedSectors ||
               _sector == unsigned(_geometry.reservedSectors + _geometry.sectorsPerFat)) {
        DskBuildFirstFatSector(scratch, _geometry);
    } else {
        std::memset(scratch, 0, 512);
    }
    FRESULT r = f_write(&_destination, scratch, 512, &count);
    if (r != FR_OK) return Finish(Status(r));
    if (count != 512) return Finish(DFILE_FULL);
    ++_sector;
    return _sector == _sectors ? Finish(DFILE_OK) : uint8_t(DFILE_MORE);
}
