#pragma once
#include "RomStorage.hxx"
#include "MountResult.hxx"
#include "ff.h"
MountResult BuildRomStorage(FIL& file, uint8_t* workspace, size_t capacity, RomStorage& out, bool forceMapped = false);
