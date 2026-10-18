#pragma once

#include "KUCSave.hxx"
#include "KUCSectorSave.hxx"

// Physical page map of a preallocated FAT file. File metadata and the FTL stay
// frozen during cartridge execution; only these owned bytes may be
// programmed with NOR1->0 transitions. Mapping entries are 512-byte offsets from XIP_BASE.
class KUCSaveStorage {
public:
    KUCSaveStorage(const uint16_t* pages, uint32_t capacity)
        : pages_(pages), capacity_(capacity) {}

    static bool SupportedChip(bool allowUnknown = false);
    KUCSaveIO IO() { return {this, capacity_, Read, ProgramPage, Pending, Service}; }
    KUCSectorSaveIO SectorIO() { return {this, capacity_, Read, Program, Pending, Service}; }

private:
    static bool Read(void* context, uint32_t offset, uint8_t* data, uint32_t size);
    static bool ProgramPage(void* context, uint32_t offset, const uint8_t* data);
    static bool Program(void* context, uint32_t offset, const uint8_t* data, uint32_t size);
    static bool Pending(void* context);
    static bool Service(void* context);
    static void ProgramSpan(uint32_t address, const uint8_t* data, unsigned size);
    bool Address(uint32_t offset, uint32_t& physical) const;

    const uint16_t* pages_;
    uint32_t capacity_;
    // Keep submitted spans alive across bus cycles. New allocations queue
    // 512 payload bytes,28 descriptor bytes and4 commit bytes, in that order.
    // IO() is retained for the old format's isolated host fixtures only.
    uint8_t queued_[544];
    uint32_t queuedAddress_[3] = {};
    uint16_t queuedOffset_[3] = {}, queuedSize_[3] = {};
    uint16_t queuedBytes_ = 0;
    uint16_t cursor_ = 0;
    uint8_t queuedCount_ = 0, queuedPage_ = 0;
};
