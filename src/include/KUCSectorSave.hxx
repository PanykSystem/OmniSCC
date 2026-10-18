#pragma once

#include "KUCSave.hxx"

// Frozen storage callbacks must never allocate or erase. Deferred adapters
// copy program spans before returning and drain them in submission order.
struct KUCSectorSaveIO {
    void* context;
    uint32_t capacity;
    bool (*read)(void*, uint32_t, uint8_t*, uint32_t);
    bool (*program)(void*, uint32_t, const uint8_t*, uint32_t);
    bool (*pending)(void*) = nullptr;
    bool (*service)(void*) = nullptr;
};

class KUCSectorSave {
public:
    // 128 KiB of payload plus a header and 512 metadata records.
    static constexpr uint32_t Capacity = 147968;
    static constexpr size_t IndexEntries = 16384;
    static constexpr uint32_t MaxSlots = 512;
    static constexpr uint32_t DataSlots = 256;
    static constexpr uint32_t DataOffset = 16896;
    KUCSectorSave(KUCSectorSaveIO io, uint16_t* index, size_t entries,
                  uint8_t (*original)(void*, uint32_t) = nullptr,
                  void* originalContext = nullptr)
        : io_(io), index_(index), entries_(entries), original_(original),
          originalContext_(originalContext)
    {
    }
    KUCSaveResult Create(const uint8_t digest[32], uint32_t romSize);
    KUCSaveResult Open(const uint8_t digest[32], uint32_t romSize);
    bool ReadByte(uint32_t address, uint8_t& value) const;
    KUCSaveResult Program(uint32_t address, const uint8_t* data, uint32_t size);
    KUCSaveResult Erase(uint32_t address, uint32_t size);
    bool Pending() const;
    KUCSaveResult Service();
    uint32_t RemainingSlots() const;
    uint32_t RemainingDataSlots() const;
    bool Healthy() const;
private:
    bool ValidIO() const;
    void Clear();
    bool Effective(uint32_t address, uint8_t& value) const;
    KUCSaveResult Append(uint32_t address, uint32_t size, const uint8_t* sector);
    void Complete();
    KUCSectorSaveIO io_;
    uint16_t* index_;
    size_t entries_;
    uint8_t (*original_)(void*, uint32_t);
    void* originalContext_;
    uint16_t next_ = 1;
    uint16_t nextPayload_ = 1;
    uint32_t pendingAddress_ = 0;
    uint32_t pendingSize_ = 0;
    bool readable_ = false;
    bool writable_ = false;
    bool pending_ = false;
    bool pendingAppend_ = false;
    bool pendingErase_ = false;
    mutable bool readFault_ = false;
};
