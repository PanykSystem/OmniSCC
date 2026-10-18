#include "KUCSave.hxx"

#include "KUCRuntime.hxx"

namespace {
constexpr uint32_t HeaderSize = 512;
constexpr uint32_t SlotSize = 512;
constexpr uint32_t PayloadMagic = 0x31524b4f;
constexpr uint32_t CommitMagic = 0x31434b4f;

uint32_t KUC_RAM(Get32)(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8)
         | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

void KUC_RAM(Put32)(uint8_t* bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[i] = uint8_t(value >> (8 * i));
}

uint32_t KUC_RAM(CRC32)(const uint8_t* bytes, size_t size)
{
    // Reflected IEEE CRC-32; no flash-resident lookup table in the write path.
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

bool KUC_RAM(Blank)(const uint8_t* bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i) if (bytes[i] != 0xff) return false;
    return true;
}

uint32_t KUC_RAM(SlotOffset)(uint16_t slot)
{
    return HeaderSize + (uint32_t(slot) - 1) * SlotSize;
}
} // namespace

bool KUCSave::ValidIO() const
{
    return index_ && entries_ >= IndexEntries && io_.read && io_.programPage
        && io_.capacity >= 1024 && io_.capacity % 512 == 0
        && (io_.capacity - HeaderSize) / SlotSize < 65535
        && bool(io_.pending) == bool(io_.service);
}

void KUCSave::ClearIndex()
{
    readable_ = writable_ = readFault_ = pending_ = false;
    next_ = 1;
    if (index_ && entries_ >= IndexEntries) {
        for (size_t i = 0; i < IndexEntries; ++i) index_[i] = 0;
    }
}

KUCSaveResult KUCSave::Create(const uint8_t digest[32], uint32_t romSize)
{
    if (pending_) return KUCSaveResult::NeedsCompaction;
    ClearIndex();
    if (!ValidIO() || !digest || !romSize || romSize > 0x800000) return KUCSaveResult::Invalid;
    uint8_t page[256];
    // Creating a journal is intentionally not a recovery/format operation. Only
    // a completely erased new allocation may be given a new identity.
    for (uint32_t offset = 0; offset < io_.capacity; offset += sizeof(page)) {
        if (!io_.read(io_.context, offset, page, sizeof(page))) return KUCSaveResult::IoError;
        if (!Blank(page, sizeof(page))) return KUCSaveResult::NotBlank;
    }
    for (auto& byte : page) byte = 0xff;
    const char magic[] = "OSKUC002";
    for (unsigned i = 0; i < 8; ++i) page[i] = uint8_t(magic[i]);
    Put32(page + 8, 2);
    Put32(page + 12, io_.capacity);
    Put32(page + 16, romSize);
    for (unsigned i = 0; i < 32; ++i) page[20 + i] = digest[i];
    Put32(page + 252, CRC32(page, 252));
    if (!io_.programPage(io_.context, 0, page)) return KUCSaveResult::IoError;
    return Open(digest, romSize);
}

void KUC_RAM(KUCSave::Apply)(uint16_t slot, uint32_t address, uint32_t size, bool erase)
{
    if (!erase) index_[address >> 9] = slot;
    else for (uint32_t page = address >> 9; page < (address + size) >> 9; ++page) index_[page] = slot;
}

KUCSaveResult KUCSave::Open(const uint8_t digest[32], uint32_t romSize)
{
    if (pending_) return KUCSaveResult::NeedsCompaction;
    ClearIndex();
    if (!ValidIO() || !digest || !romSize || romSize > 0x800000) return KUCSaveResult::Invalid;
    uint8_t payload[256], commit[256];
    if (!io_.read(io_.context, 0, payload, 256)) return KUCSaveResult::IoError;
    const char magic[] = "OSKUC002";
    for (unsigned i = 0; i < 8; ++i) if (payload[i] != uint8_t(magic[i])) return KUCSaveResult::Corrupt;
    if (Get32(payload + 8) != 2 || Get32(payload + 12) != io_.capacity
        || Get32(payload + 252) != CRC32(payload, 252)) return KUCSaveResult::Corrupt;
    if (Get32(payload + 16) != romSize) return KUCSaveResult::WrongROM;
    for (unsigned i = 0; i < 32; ++i) if (payload[20 + i] != digest[i]) return KUCSaveResult::WrongROM;

    bool ended = false;
    bool torn = false;
    for (uint32_t slot = 1; SlotOffset(uint16_t(slot)) < io_.capacity; ++slot) {
        const uint32_t offset = SlotOffset(uint16_t(slot));
        if (!io_.read(io_.context, offset, payload, 256)
            || !io_.read(io_.context, offset + 256, commit, 256)) return KUCSaveResult::IoError;
        const bool empty = Blank(payload, 256) && Blank(commit, 256);
        if (ended) {
            if (!empty) return KUCSaveResult::Corrupt; // Damage inside a journal is not a torn tail.
            continue;
        }
        if (empty) {
            next_ = uint16_t(slot);
            ended = true;
            continue;
        }
        if (Get32(commit) != CommitMagic || Get32(commit + 252) != CRC32(commit, 252)) {
            torn = true;
            ended = true;
            next_ = uint16_t(slot);
            continue;
        }
        const uint32_t address = Get32(payload + 4);
        const uint32_t size = Get32(payload + 8);
        const uint32_t kind = Get32(payload + 12);
        const uint32_t previous = Get32(payload + 16);
        const bool validRange = address < 0x800000 && size && size <= 0x800000 - address;
        const bool validShape = kind == 1
            ? size == 32 && address % 32 == 0
            : kind == 2 && address % 512 == 0 && size % 512 == 0;
        if (Get32(payload) != PayloadMagic || Get32(payload + 20) != slot
            || Get32(commit + 8) != slot || Get32(commit + 252) != CRC32(commit, 252)
            || Get32(payload + 252) != CRC32(payload, 252)
            || Get32(commit + 4) != Get32(payload + 252) || !validRange || !validShape
            || (kind == 1 && previous != index_[address >> 9])) return KUCSaveResult::Corrupt;
        if (kind == 1) {
            uint8_t prior[96] = {};
            if (previous && !io_.read(io_.context, SlotOffset(uint16_t(previous)), prior, sizeof(prior)))
                return KUCSaveResult::IoError;
            for (unsigned block = 0; block < 16; ++block) {
                uint16_t expected = 0;
                if (previous) {
                    expected = Get32(prior + 12) == 2 ? uint16_t(previous)
                        : uint16_t(prior[64 + 2 * block] | (uint16_t(prior[65 + 2 * block]) << 8));
                }
                if (block == ((address & 511) >> 5)) expected = uint16_t(slot);
                const uint16_t actual = uint16_t(payload[64 + 2 * block]
                                               | (uint16_t(payload[65 + 2 * block]) << 8));
                if (actual != expected) return KUCSaveResult::Corrupt;
            }
        }
        Apply(uint16_t(slot), address, size, kind == 2);
        next_ = uint16_t(slot + 1);
    }
    readable_ = true;
    writable_ = !torn;
    return torn ? KUCSaveResult::TornTail : KUCSaveResult::Ok;
}

bool KUC_RAM(KUCSave::ReadByte)(uint32_t address, uint8_t& value) const
{
    if (!readable_ || address >= 0x800000) return false;
    const uint16_t latest = index_[address >> 9];
    if (!latest) return false;
    uint8_t header[16];
    auto read = [&](uint32_t offset, uint8_t* bytes, uint32_t size) {
        if (io_.read(io_.context, offset, bytes, size)) return true;
        readFault_ = true;
        value = 0xff;
        return false;
    };
    if (!read(SlotOffset(latest), header, sizeof(header))) return true;
    if (Get32(header + 12) == 2) { value = 0xff; return true; }
    uint8_t route[2];
    if (!read(SlotOffset(latest) + 64 + ((address & 511) >> 5) * 2, route, 2)) return true;
    const uint16_t block = uint16_t(route[0] | (uint16_t(route[1]) << 8));
    if (!block) return false;
    if (block != latest) {
        if (!read(SlotOffset(block), header, sizeof(header))) return true;
        if (Get32(header + 12) == 2) { value = 0xff; return true; }
    }
    read(SlotOffset(block) + 32 + (address & 31), &value, 1);
    return true;
}

KUCSaveResult KUC_RAM(KUCSave::Append)(uint32_t address, uint32_t size, const uint8_t* data)
{
    if (!readable_ || readFault_) return KUCSaveResult::Invalid;
    if (!writable_) return KUCSaveResult::NeedsCompaction;
    const uint32_t offset = SlotOffset(next_);
    if (offset >= io_.capacity) return KUCSaveResult::Full;
    uint8_t payload[256], commit[256];
    for (unsigned i = 0; i < 256; ++i) payload[i] = commit[i] = 0xff;
    Put32(payload, PayloadMagic);
    Put32(payload + 4, address);
    Put32(payload + 8, size);
    Put32(payload + 12, data ? 1 : 2);
    Put32(payload + 16, data ? index_[address >> 9] : 0);
    Put32(payload + 20, next_);
    if (data) {
        for (uint32_t i = 0; i < size; ++i) payload[32 + i] = data[i];
        const uint16_t previous = index_[address >> 9];
        uint8_t prior[96] = {};
        if (previous && !io_.read(io_.context, SlotOffset(previous), prior, sizeof(prior)))
            return KUCSaveResult::IoError;
        // Each record carries a small routing table to the latest FULL 32-byte
        // block. Reads take a fixed number of accesses, even after thousands of
        // writes to the same byte; a linked journal scan would violate /WAIT.
        for (unsigned block = 0; block < 16; ++block) {
            uint16_t target = 0;
            if (previous) target = Get32(prior + 12) == 2 ? previous
                : uint16_t(prior[64 + 2 * block] | (uint16_t(prior[65 + 2 * block]) << 8));
            if (block == ((address & 511) >> 5)) target = next_;
            payload[64 + 2 * block] = uint8_t(target);
            payload[65 + 2 * block] = uint8_t(target >> 8);
        }
    }
    const uint32_t checksum = CRC32(payload, 252);
    Put32(payload + 252, checksum);
    Put32(commit, CommitMagic);
    Put32(commit + 4, checksum);
    Put32(commit + 8, next_);
    Put32(commit + 252, CRC32(commit, 252));
    // A failed operation may have partially programmed either page. Do not
    // reuse the slot, and do not expose it through the live overlay index.
    writable_ = false;
    if (!io_.programPage(io_.context, offset, payload)
        || !io_.programPage(io_.context, offset + 256, commit)) return KUCSaveResult::IoError;
    pendingAddress_ = address;
    pendingSize_ = size;
    pendingErase_ = !data;
    pending_ = true;
    if (!io_.pending || !io_.pending(io_.context)) CommitPending();
    return KUCSaveResult::Ok;
}

void KUC_RAM(KUCSave::CommitPending)()
{
    Apply(next_, pendingAddress_, pendingSize_, pendingErase_);
    ++next_;
    pending_ = false;
    writable_ = true;
}

KUCSaveResult KUC_RAM(KUCSave::Service)()
{
    if (!pending_) return KUCSaveResult::Ok;
    if (!io_.service || !io_.service(io_.context)) {
        pending_ = false;
        // Preserve the old index. A partial tail must never be reused.
        return KUCSaveResult::IoError;
    }
    if (!io_.pending(io_.context)) CommitPending();
    return KUCSaveResult::Ok;
}

KUCSaveResult KUC_RAM(KUCSave::Program)(uint32_t address, const uint8_t* data, uint32_t size)
{
    if (!data || size != 32 || address % 32 || address >= 0x800000 || size > 0x800000 - address) return KUCSaveResult::Invalid;
    return Append(address, size, data);
}

KUCSaveResult KUC_RAM(KUCSave::Erase)(uint32_t address, uint32_t size)
{
    if (!size || address >= 0x800000 || size > 0x800000 - address
        || address % 512 || size % 512) return KUCSaveResult::Invalid;
    return Append(address, size, nullptr);
}
