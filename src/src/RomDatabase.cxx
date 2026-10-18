#include "RomDatabase.hxx"
#include "mapper_database.hxx"
#include <cstring>
#include <cstdio>

namespace {
char Upper(char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; }
bool Equal(const char* a, const char* b) {
    while (*a && Upper(*a) == Upper(*b)) { ++a; ++b; }
    return Upper(*a) == Upper(*b);
}
const char* TagAt(const char* p, const char*& after)
{
    if (*p != '[') return nullptr;
    for (const char* name : MapperDatabaseNames) {
        if (!*name) continue;
        const char* a = p + 1;
        const char* b = name;
        while (*b && Upper(*a) == Upper(*b)) { ++a; ++b; }
        if (!*b && *a == ']') { after = a + 1; return name; }
    }
    if (Upper(p[1]) == 'S' && Upper(p[2]) == 'C' && Upper(p[3]) == 'C'
        && p[4] == '+' && p[5] == ']') { after = p + 6; return "SCCPlus"; }
    const char* longName = "KonamiUltimateCollection";
    const char* a = p + 1;
    while (*longName && Upper(*a) == Upper(*longName)) { ++a; ++longName; }
    if (!*longName && *a == ']') { after = a + 1; return "KUC"; }
    return nullptr;
}
}

namespace {
uint32_t DatabaseGet32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint32_t CRC(uint32_t crc, const uint8_t* p, size_t size) {
    while (size--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return crc;
}
}

bool RomDatabase::Open(RomDatabaseIO io)
{
    io_ = {}; count_ = names_ = records_ = 0;
    uint8_t header[40];
    if (!io.read || io.size < sizeof(header) || !io.read(io.context, 0, header, sizeof(header))
        || std::memcmp(header, "OSROMDB1", 8) || DatabaseGet32(header + 8) != 1
        || DatabaseGet32(header + 12) != 8 || DatabaseGet32(header + 36) != ~CRC(~0u, header, 36)) return false;
    const uint32_t count = DatabaseGet32(header + 16), names = DatabaseGet32(header + 20), records = DatabaseGet32(header + 24);
    if (!count || names < 2 || names > 256 || records != 40 + names * 32
        || DatabaseGet32(header + 28) != io.size || records > io.size
        || uint64_t(count) * 8 != io.size - records) return false;
    uint8_t buffer[512];
    uint32_t crc = ~0u;
    for (uint32_t offset = 40; offset < io.size;) {
        size_t amount = io.size - offset;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (!io.read(io.context, offset, buffer, amount)) return false;
        crc = CRC(crc, buffer, amount); offset += uint32_t(amount);
    }
    if (~crc != DatabaseGet32(header + 32)) return false;
    for (uint32_t id = 0; id < names; ++id) {
        if (!io.read(io.context, 40 + id * 32, buffer, 32)
            || !std::memchr(buffer, 0, 32) || (id && !buffer[0]) || (!id && buffer[0])) return false;
        for (unsigned n = 0; buffer[n]; ++n) {
            const char c = char(buffer[n]);
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        }
    }
    uint8_t previous[8] = {};
    for (uint32_t n = 0; n < count;) {
        const uint32_t rows = count - n > 64 ? 64 : count - n;
        if (!io.read(io.context, records + n * 8, buffer, rows * 8)) return false;
        for (uint32_t i = 0; i < rows; ++i) {
            const uint8_t* row = buffer + i * 8;
            if (!row[7] || row[7] >= names || ((n || i) && std::memcmp(previous, row, 7) >= 0)) return false;
            std::memcpy(previous, row, 8);
        }
        n += rows;
    }
    io_ = io; count_ = count; names_ = names; records_ = records;
    return true;
}

bool RomDatabase::Lookup(const uint8_t digest[20], char name[32]) const
{
    if (!io_.read) return false;
    uint32_t low = 0, high = count_;
    uint8_t row[8];
    while (low < high) {
        const uint32_t mid = low + (high - low) / 2;
        if (!io_.read(io_.context, records_ + mid * 8, row, sizeof(row))) return false;
        const int order = std::memcmp(digest, row, 7);
        if (!order) return row[7] < names_
            && io_.read(io_.context, 40 + row[7] * 32, reinterpret_cast<uint8_t*>(name), 32);
        if (order < 0) high = mid;
        else low = mid + 1;
    }
    return false;
}

const char* RomKnownMapperName(const char* name)
{
    for (const char* candidate : MapperDatabaseNames)
        if (*candidate && Equal(candidate, name)) return candidate;
    if (Equal(name, "KonamiUltimateCollection")) return "KUC";
    return nullptr;
}

const char* RomMapperTag(const char* filename)
{
    const char* base = std::strrchr(filename, '/');
    if (base) filename = base + 1;
    for (const char* p = filename; *p; ++p) {
        const char* after;
        if (const char* tag = TagAt(p, after)) return tag;
    }
    return nullptr;
}

void RomStripMapperTags(const char* source, char* output, size_t capacity)
{
    if (!capacity) return;
    size_t used = 0;
    while (*source) {
        const char* after;
        if (TagAt(source, after)) { source = after; continue; }
        if (used + 1 < capacity) output[used++] = *source;
        ++source;
    }
    output[used] = 0;
}

bool RomSameUntaggedName(const char* left, const char* right)
{
    char a[256], b[256];
    RomStripMapperTags(left, a, sizeof(a));
    RomStripMapperTags(right, b, sizeof(b));
    return Equal(a, b);
}

bool RomTaggedPath(const char* path, const char* mapper, char* output, size_t capacity)
{
    const char* base = std::strrchr(path, '/');
    base = base ? base + 1 : path;
    const char* extension = std::strrchr(base, '.');
    if (!extension) extension = path + std::strlen(path);
    // The existing menu stores at most 31 basename characters. Do not turn a
    // complete displayed name into an 8.3 alias merely by adding our tag.
    if (std::strlen(base) + std::strlen(mapper) + 2 > 31) return false;
    const int count = std::snprintf(output, capacity, "%.*s[%s]%s",
                                    int(extension - path), path, mapper, extension);
    return count >= 0 && size_t(count) < capacity;
}
