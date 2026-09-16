#pragma once
#include "DiskImage.hxx"
#include "DiskFormat.hxx"

#include "DiskFileProtocol.hxx"
static_assert(DISK_PATH_MAX == DFILE_PATH_SIZE, "file protocol path size");

class CDiskFiles {
public:
    CDiskFiles(CDiskImage& a, CDiskImage& b) : _a(a), _b(b) {}
    ~CDiskFiles() { Abort(); }
    uint8_t Delete(const char* path);
    uint8_t Copy(const char* source, const char* destination);
    uint8_t Create(const char* path, uint16_t sizeKiB);
    uint8_t Step(uint8_t* scratch); // at least 512 bytes, outside BASIC code
    void Abort();                 // discards only the file this operation made
private:
    static uint8_t Status(FRESULT result);
    uint8_t OpenDestination(const char* path);
    uint8_t Finish(uint8_t status);
    CDiskImage& _a;
    CDiskImage& _b;
    FIL _source{};
    FIL _destination{};
    bool _sourceOpen = false;
    bool _destinationOpen = false;
    char _path[DISK_PATH_MAX]{};
    uint32_t _sector = 0, _sectors = 0;
    DskGeometry _geometry{};
};
