#pragma once
#include <cstddef>
#include <cstdint>

struct RomDatabaseIO {
    void* context;
    uint32_t size;
    bool (*read)(void*, uint32_t, uint8_t*, size_t);
};

// Validated file view. No persistent database-sized RAM allocation.
class RomDatabase {
public:
    bool Open(RomDatabaseIO io);
    bool Lookup(const uint8_t digest[20], char name[32]) const;
private:
    RomDatabaseIO io_{};
    uint32_t count_ = 0, names_ = 0, records_ = 0;
};
const char* RomKnownMapperName(const char* name);
const char* RomMapperTag(const char* filename);
void RomStripMapperTags(const char* source, char* output, size_t capacity);
bool RomSameUntaggedName(const char* left, const char* right);
// Restrict auto-renamed basenames to the browser's complete-name field.
bool RomTaggedPath(const char* path, const char* mapper, char* output, size_t capacity);
