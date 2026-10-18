#include "KUCSectorSave.hxx"
#include "KUCRuntime.hxx"

namespace {
constexpr uint32_t SectorMagic = 0x33534b4f;

uint32_t KUC_RAM(SectorGet32)(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8)
         | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

void KUC_RAM(SectorPut32)(uint8_t* bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        bytes[i] = uint8_t(value >> (8 * i));
    }
}

uint32_t KUC_RAM(SectorCRC)(const uint8_t* bytes, size_t size)
{
    uint32_t crc = ~0u;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

bool KUC_RAM(SectorBlank)(const uint8_t* bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] != 0xff) return false;
    }
    return true;
}

uint32_t KUC_RAM(SectorDataOffset)(uint16_t slot)
{
    return KUCSectorSave::DataOffset + (uint32_t(slot) - 1) * 512;
}

uint32_t KUC_RAM(SectorMetadataOffset)(uint16_t slot)
{
    return 512 + (uint32_t(slot) - 1) * 32;
}
} // namespace

bool KUC_RAM(KUCSectorSave::ValidIO)() const
{
    return index_ && entries_ >= IndexEntries && io_.capacity == Capacity
        && io_.read && io_.program && bool(io_.pending) == bool(io_.service);
}

void KUC_RAM(KUCSectorSave::Clear)()
{
    readable_ = writable_ = pending_ = readFault_ = false;
    next_ = nextPayload_ = 1;
    if (index_ && entries_ >= IndexEntries) {
        for (size_t i = 0; i < IndexEntries; ++i) {
            index_[i] = 0;
        }
    }
}

KUCSaveResult KUCSectorSave::Create(const uint8_t digest[32], uint32_t romSize)
{
    if (pending_) return KUCSaveResult::NeedsCompaction;
    Clear();
    if (!ValidIO() || !digest || !romSize || romSize > 0x800000) {
        return KUCSaveResult::Invalid;
    }
    uint8_t header[256];
    for (uint32_t offset = 0; offset < Capacity; offset += 256) {
        if (!io_.read(io_.context, offset, header, 256)) return KUCSaveResult::IoError;
        if (!SectorBlank(header, 256)) return KUCSaveResult::NotBlank;
    }
    for (auto& byte : header) byte = 0xff;
    const char magic[] = "OSKUC003";
    for (unsigned i = 0; i < 8; ++i) header[i] = uint8_t(magic[i]);
    SectorPut32(header + 8, 3);
    SectorPut32(header + 12, Capacity);
    SectorPut32(header + 16, romSize);
    for (unsigned i = 0; i < 32; ++i) header[20 + i] = digest[i];
    SectorPut32(header + 252, SectorCRC(header, 252));
    if (!io_.program(io_.context, 0, header, 256)) return KUCSaveResult::IoError;
    while (io_.pending && io_.pending(io_.context)) {
        if (!io_.service(io_.context)) return KUCSaveResult::IoError;
    }
    return Open(digest, romSize);
}

KUCSaveResult KUCSectorSave::Open(const uint8_t digest[32], uint32_t romSize)
{
    if (pending_) return KUCSaveResult::NeedsCompaction;
    Clear();
    if (!ValidIO() || !digest || !romSize || romSize > 0x800000) {
        return KUCSaveResult::Invalid;
    }
    uint8_t payload[512];
    if (!io_.read(io_.context, 0, payload, 256)) return KUCSaveResult::IoError;
    const char magic[] = "OSKUC003";
    for (unsigned i = 0; i < 8; ++i) {
        if (payload[i] != uint8_t(magic[i])) return KUCSaveResult::Corrupt;
    }
    if (SectorGet32(payload + 8) != 3 || SectorGet32(payload + 12) != Capacity
        || SectorGet32(payload + 252) != SectorCRC(payload, 252)) {
        return KUCSaveResult::Corrupt;
    }
    if (SectorGet32(payload + 16) != romSize) return KUCSaveResult::WrongROM;
    for (unsigned i = 0; i < 32; ++i) {
        if (payload[20 + i] != digest[i]) return KUCSaveResult::WrongROM;
    }

    // Metadata records and payload sectors have separate allocation cursors.
    // Erase markers consume only metadata, preserving all 128 KiB of payload.
    bool torn = false;
    bool gap = false;
    for (uint16_t slot = 1; slot <= MaxSlots; ++slot) {
        uint8_t descriptor[32];
        if (!io_.read(io_.context, SectorMetadataOffset(slot), descriptor, 32)) {
            return KUCSaveResult::IoError;
        }
        if (SectorBlank(descriptor, 32)) {
            gap = true;
            continue;
        }
        next_ = slot + 1;
        if (SectorGet32(descriptor + 28) != 0) {
            torn = true;
            continue;
        }
        const uint32_t address = SectorGet32(descriptor + 8);
        const uint32_t size = SectorGet32(descriptor + 12);
        const uint32_t kind = SectorGet32(descriptor + 16);
        const uint32_t payloadID = SectorGet32(descriptor + 20);
        const bool validPayload = kind == 1
            ? payloadID == nextPayload_ && payloadID <= DataSlots
            : payloadID == 0;
        if (gap || torn || SectorGet32(descriptor) != SectorMagic
            || SectorGet32(descriptor + 4) != slot || !validPayload
            || SectorGet32(descriptor + 24) != SectorCRC(descriptor, 24)
            || address >= 0x800000 || !size || size > 0x800000 - address
            || address % 512 || (kind == 1 ? size != 512 : kind != 2 || size % 512)) {
            return KUCSaveResult::Corrupt;
        }
        for (uint32_t i = address / 512; i < (address + size) / 512; ++i) {
            index_[i] = kind == 1 ? uint16_t(payloadID) : 0xffff;
        }
        if (kind == 1) ++nextPayload_;
    }
    // A cut before metadata starts can leave an orphan payload. Do not reuse
    // any such allocation; menu compaction can preserve the committed state.
    for (uint16_t slot = nextPayload_; slot <= DataSlots; ++slot) {
        if (!io_.read(io_.context, SectorDataOffset(slot), payload, 512)) {
            return KUCSaveResult::IoError;
        }
        if (!SectorBlank(payload, 512)) torn = true;
    }
    readable_ = true;
    writable_ = !torn;
    return torn ? KUCSaveResult::TornTail : KUCSaveResult::Ok;
}

bool KUC_RAM(KUCSectorSave::ReadByte)(uint32_t address, uint8_t& value) const
{
    if (!readable_ || address >= 0x800000) return false;
    const uint16_t slot = index_[address / 512];
    if (!slot) return false;
    value = 0xff;
    if (slot != 0xffff
        && !io_.read(io_.context, SectorDataOffset(slot) + address % 512, &value, 1)) {
        value = 0xff;
        readFault_ = true;
    }
    return true;
}

bool KUC_RAM(KUCSectorSave::Effective)(uint32_t address, uint8_t& value) const
{
    if (!ReadByte(address, value)) {
        value = original_ ? original_(originalContext_, address) : 0xff;
    }
    return !readFault_;
}

bool KUC_RAM(KUCSectorSave::Pending)() const
{
    return pending_;
}

bool KUC_RAM(KUCSectorSave::Healthy)() const
{
    return readable_ && !readFault_;
}

uint32_t KUC_RAM(KUCSectorSave::RemainingSlots)() const
{
    if (!writable_ || next_ > MaxSlots || nextPayload_ > DataSlots) return 0;
    const uint32_t metadata = MaxSlots - next_ + 1;
    const uint32_t payload = DataSlots - nextPayload_ + 1;
    return metadata < payload ? metadata : payload;
}

uint32_t KUC_RAM(KUCSectorSave::RemainingDataSlots)() const
{
    return writable_ && nextPayload_ <= DataSlots ? DataSlots - nextPayload_ + 1 : 0;
}

void KUC_RAM(KUCSectorSave::Complete)()
{
    if (pendingAppend_) {
        for (uint32_t i = pendingAddress_ / 512;
             i < (pendingAddress_ + pendingSize_) / 512; ++i) {
            index_[i] = pendingErase_ ? 0xffff : nextPayload_;
        }
        ++next_;
        if (!pendingErase_) ++nextPayload_;
    }
    pending_ = false;
    writable_ = true;
}

KUCSaveResult KUC_RAM(KUCSectorSave::Service)()
{
    if (!pending_) return KUCSaveResult::Ok;
    if (!io_.service || !io_.service(io_.context)) {
        pending_ = false;
        writable_ = false;
        return KUCSaveResult::IoError;
    }
    if (!io_.pending(io_.context)) Complete();
    return KUCSaveResult::Ok;
}

KUCSaveResult KUC_RAM(KUCSectorSave::Append)(uint32_t address, uint32_t size,
                                           const uint8_t* sector)
{
    if (next_ > MaxSlots || (sector && nextPayload_ > DataSlots)) return KUCSaveResult::Full;
    uint8_t descriptor[32] = {};
    SectorPut32(descriptor, SectorMagic);
    SectorPut32(descriptor + 4, next_);
    SectorPut32(descriptor + 8, address);
    SectorPut32(descriptor + 12, size);
    SectorPut32(descriptor + 16, sector ? 1 : 2);
    SectorPut32(descriptor + 20, sector ? nextPayload_ : 0);
    SectorPut32(descriptor + 24, SectorCRC(descriptor, 24));
    // Adapters copy each span before returning. The commit token is queued last.
    writable_ = false;
    if ((sector && !io_.program(io_.context, SectorDataOffset(nextPayload_), sector, 512))
        || !io_.program(io_.context, SectorMetadataOffset(next_), descriptor, 28)
        || !io_.program(io_.context, SectorMetadataOffset(next_) + 28, descriptor + 28, 4)) {
        return KUCSaveResult::IoError;
    }
    pendingAddress_ = address;
    pendingSize_ = size;
    pendingErase_ = !sector;
    pendingAppend_ = true;
    pending_ = true;
    if (!io_.pending || !io_.pending(io_.context)) Complete();
    return KUCSaveResult::Ok;
}

KUCSaveResult KUC_RAM(KUCSectorSave::Program)(uint32_t address, const uint8_t* data,
                                            uint32_t size)
{
    if (!data || !size || size > 512 || address >= 0x800000 || size > 512 - address % 512) {
        return KUCSaveResult::Invalid;
    }
    if (!readable_ || readFault_) return KUCSaveResult::Invalid;
    if (!writable_ || pending_) return KUCSaveResult::NeedsCompaction;
    bool changed = false;
    for (uint32_t i = 0; i < size; ++i) {
        uint8_t old;
        if (!Effective(address + i, old)) {
            writable_ = false;
            return KUCSaveResult::IoError;
        }
        if ((old & data[i]) != data[i]) return KUCSaveResult::Invalid;
        changed |= old != data[i];
    }
    if (!changed) return KUCSaveResult::Ok;
    const uint16_t slot = index_[address / 512];
    // Existing payloads support NOR bit clearing even after descriptor space
    // is exhausted. No payload checksum is used because these bytes mutate.
    if (slot && slot != 0xffff) {
        writable_ = false;
        if (!io_.program(io_.context, SectorDataOffset(slot) + address % 512, data, size)) {
            return KUCSaveResult::IoError;
        }
        pendingAppend_ = false;
        pending_ = true;
        if (!io_.pending || !io_.pending(io_.context)) Complete();
        return KUCSaveResult::Ok;
    }
    if (next_ > MaxSlots || nextPayload_ > DataSlots) return KUCSaveResult::Full;
    uint8_t sector[512];
    const uint32_t base = address & ~511u;
    for (uint32_t i = 0; i < 512; ++i) {
        if (!Effective(base + i, sector[i])) {
            writable_ = false;
            return KUCSaveResult::IoError;
        }
    }
    for (uint32_t i = 0; i < size; ++i) sector[address % 512 + i] = data[i];
    return Append(base, 512, sector);
}

KUCSaveResult KUC_RAM(KUCSectorSave::Erase)(uint32_t address, uint32_t size)
{
    if (!size || address >= 0x800000 || size > 0x800000 - address
        || address % 512 || size % 512) {
        return KUCSaveResult::Invalid;
    }
    if (!readable_ || readFault_) return KUCSaveResult::Invalid;
    if (!writable_ || pending_) return KUCSaveResult::NeedsCompaction;
    // A repeated logical erase needs no new descriptor, even when full.
    bool alreadyErased = true;
    for (uint32_t i = address / 512; i < (address + size) / 512; ++i) {
        if (index_[i] != 0xffff) {
            alreadyErased = false;
            break;
        }
    }
    if (alreadyErased) return KUCSaveResult::Ok;
    return Append(address, size, nullptr);
}
