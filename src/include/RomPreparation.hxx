#pragma once
#include "MountResult.hxx"
#include <cstddef>
#include <cstdint>

// Resolve a real LFN (also through an old untagged path), then explicit tag,
// SHA-1 database and optional rename. Runs before any final ROM XIP mapping.
MountResult PrepareRomPath(char* path, size_t capacity, const char*& mapper,
                          const char* (*lookup)(const uint8_t*) = nullptr,
                          const char* databasePath = "/romdb.bin");
