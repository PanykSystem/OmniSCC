#include "OmniSCC.hxx"
#include "RomPreparation.hxx"
#include <cstring>

bool COmniSCC::SetRomDatabasePath(const char* path)
{
    if (!path || std::strlen(path) > ROM_DATABASE_PATH_MAX) return false;
    std::strcpy(_romDatabasePath, path);
    return true;
}

bool COmniSCC::PrepareROM(char* path, size_t capacity, CartType& type)
{
    const char* mapper = nullptr;
    _mountResult = PrepareRomPath(path, capacity, mapper, nullptr, _romDatabasePath);
    if (_mountResult != MountResult::Ok) return false;
    type = CT_LLast; // The existing signature/instruction heuristic runs later.
    if (!mapper) return true;
    if (!std::strcmp(mapper, "KUC")) type = CT_KUC;
    else if (!std::strcmp(mapper, "Konami")) type = CT_Konami;
    else if (!std::strcmp(mapper, "KonamiSCC")) type = CT_KonamiSCC;
    else if (!std::strcmp(mapper, "ASCII8")) type = CT_ASCII8;
    else if (!std::strcmp(mapper, "ASCII16")) type = CT_ASCII16;
    else if (!std::strcmp(mapper, "SCCI")) type = CT_SCCI;
    else if (!std::strcmp(mapper, "SCCPlus")) type = CT_SCCPlus;
    else if (!std::strcmp(mapper, "8kB")) type = CT_Generic8kB;
    else if (!std::strcmp(mapper, "Plain") || !std::strcmp(mapper, "Mirrored")) type = CT_Plain;
    else if (!std::strcmp(mapper, "Normal")) type = CT_Normal;
    else if (!std::strcmp(mapper, "0x0000")) type = CT_Normal0000;
    else if (!std::strcmp(mapper, "0x4000")) type = CT_Normal4000;
    else if (!std::strcmp(mapper, "0x8000")) type = CT_Normal8000;
    else if (!std::strcmp(mapper, "Mirrored4000")) type = CT_Mirrored4000;
    else { _mountResult = MountResult::UnsupportedMapper; return false; }
    return true;
}
