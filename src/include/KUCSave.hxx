#pragma once

#include <cstddef>
#include <cstdint>

enum class KUCSaveResult : uint8_t {
    Ok, Full, Invalid, IoError, WrongROM, Corrupt, NotBlank, TornTail, NeedsCompaction
};

// The physical adapter guarantees a frozen allocation. It must program exactly
// one erased 256-byte page per call; neither callback may allocate or erase.
// An optional deferred adapter queues both pages, then advances one bounded
// physical write per service call. Live indexes change only after completion.
struct KUCSaveIO {
    void* context;
    uint32_t capacity;
    bool (*read)(void*, uint32_t, uint8_t*, uint32_t);
    bool (*programPage)(void*, uint32_t, const uint8_t*);
    bool (*pending)(void*) = nullptr;
    bool (*service)(void*) = nullptr;
};

class KUCSave {
public:
    static constexpr size_t IndexEntries = 0x800000 / 512;

    KUCSave(KUCSaveIO io, uint16_t* index, size_t entries)
        : io_(io), index_(index), entries_(entries) {}

    KUCSaveResult Create(const uint8_t digest[32], uint32_t romSize);
    KUCSaveResult Open(const uint8_t digest[32], uint32_t romSize);
    bool ReadByte(uint32_t address, uint8_t& value) const;
    KUCSaveResult Program(uint32_t address, const uint8_t* data, uint32_t size);
    KUCSaveResult Erase(uint32_t address, uint32_t size);
    bool Pending() const { return pending_; }
    KUCSaveResult Service();
    uint32_t RemainingSlots() const {
        return writable_ ? (io_.capacity - 512) / 512 - (next_ - 1) : 0;
    }
    bool Healthy() const { return readable_ && !readFault_; }

private:
    bool ValidIO() const;
    void ClearIndex();
    KUCSaveResult Append(uint32_t address, uint32_t size, const uint8_t* data);
    void CommitPending();
    void Apply(uint16_t slot, uint32_t address, uint32_t size, bool erase);

    KUCSaveIO io_;
    uint16_t* index_;
    size_t entries_;
    uint16_t next_ = 1;
    uint32_t pendingAddress_ = 0, pendingSize_ = 0;
    bool pendingErase_ = false, pending_ = false;
    bool readable_ = false;
    bool writable_ = false;
    mutable bool readFault_ = false;
};
