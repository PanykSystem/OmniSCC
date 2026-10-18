#pragma once

#include <cstdint>
#include <array>

// The command engine owns no ROM-sized buffer. Storage callbacks expose the
// original image plus its overlay. Synchronous mutations are durable on return;
// deferred adapters keep pending() true until service() has committed them.
struct KUCFlashStorage {
    void* context;
    uint8_t (*read)(void*, uint32_t);
    bool (*program)(void*, uint32_t, const uint8_t*, uint32_t);
    bool (*erase)(void*, uint32_t, uint32_t);
    bool (*pending)(void*) = nullptr;
    bool (*service)(void*) = nullptr;
    // Legacy/reference adapters may model device timing. Direct-sector saves
    // report ready after their actual durable erase marker, without500ms delay.
    uint32_t eraseMicroseconds = 500000;
};

class KUCFlash {
public:
    explicit KUCFlash(KUCFlashStorage storage) : storage_(storage) {}

    uint8_t Read(uint32_t address, uint64_t microseconds);
    void Write(uint32_t address, uint8_t value, uint64_t microseconds);
    // Debugger access must not advance a pending durable operation.
    uint8_t Peek(uint32_t address) const {
        KUCFlash shadow = *this;
        shadow.deadline_ = ~uint64_t(0);
        shadow.suspendAt_ = 0;
        return shadow.Read(address, 0);
    }
    // Ordinary ROM fetches need neither a timer read nor command decoding.
    bool ArrayRead() const {
        return mode_ == Mode::Array && operation_ == Operation::None && !eraseSuspended_;
    }
    bool Pending() const { return operation_ != Operation::None && !suspended_; }
    uint64_t NextServiceTime() const {
        if (!Pending()) return ~uint64_t(0);
        return suspendAt_ && suspendAt_ < deadline_ ? suspendAt_ : deadline_;
    }
    bool ServiceDue(uint64_t now) const { return Pending() && now >= NextServiceTime(); }
    void Service(uint64_t now) { Advance(now); }
    void Reset();
    void SetProtected(uint32_t address, bool protect);
    static uint32_t SectorStart(uint32_t address);
    static uint32_t SectorSize(uint32_t address);

    template<class Archive> void serialize(Archive& ar, unsigned) {
        uint8_t mode = uint8_t(mode_), sequence = uint8_t(sequence_), operation = uint8_t(operation_);
        ar.serialize("mode", mode, "sequence", sequence, "operation", operation,
            "deadline", deadline_, "remaining", remaining_, "eraseRemaining", eraseRemaining_,
            "eraseSuspended", eraseSuspended_, "suspendAt", suspendAt_, "eraseWindow", eraseWindow_,
            "suspended", suspended_, "toggle", toggle_, "status", status_, "count", count_,
            "received", received_, "pageSize", pageSize_, "commandSector", commandSector_,
            "programBase", programBase_, "valid", valid_, "data", data_,
            "protected", protected_, "erasePending", erasePending_);
        if constexpr (Archive::IS_LOADER) {
            mode_ = Mode(mode); sequence_ = Sequence(sequence); operation_ = Operation(operation);
            // Deferred physical writes exist only on firmware. Emulator storage
            // commits synchronously and never serializes a backend transaction.
            storagePending_ = finishError_ = false;
        }
    }
    bool ValidState() const {
        return uint8_t(mode_) <= uint8_t(Mode::Error)
            && uint8_t(sequence_) <= uint8_t(Sequence::BufferConfirm)
            && uint8_t(operation_) <= uint8_t(Operation::ChipErase)
            && count_ <= 32 && received_ <= count_ && programBase_ < 0x800000
            && commandSector_ < 0x800000 && (pageSize_ == 0 || pageSize_ == 8 || pageSize_ == 32)
            && (operation_ != Operation::Program || (pageSize_ != 0
                && programBase_ % pageSize_ == 0 && pageSize_ <= 0x800000 - programBase_))
            && (sequence_ != Sequence::ProgramData || (pageSize_ == 8 && count_ > 0 && count_ <= 4))
            && (sequence_ != Sequence::BufferData || (pageSize_ == 32 && count_ > 0 && received_ < count_))
            && (sequence_ != Sequence::BufferConfirm || (pageSize_ == 32 && count_ > 0 && received_ == count_));
    }

private:
    enum class Mode : uint8_t { Array, Identify, Query, Error };
    enum class Sequence : uint8_t {
        Idle, Unlock2, Command, EraseUnlock1, EraseUnlock2, EraseConfirm,
        ProgramData, BufferCount, BufferData, BufferConfirm
    };
    enum class Operation : uint8_t { None, Program, SectorErase, ChipErase };

    void Advance(uint64_t now);
    void StartProgram(uint64_t now);
    void CompleteMutation(uint64_t now, bool error);
    void ClearSequence();
    void Fail(bool bufferAbort = false);
    uint8_t Query(uint32_t address) const;
    unsigned SectorIndex(uint32_t address) const;
    bool Protected(uint32_t address) const;
    void MarkErase(uint32_t address);
    bool PendingErase(uint32_t address) const;

    KUCFlashStorage storage_;
    Mode mode_ = Mode::Array;
    Sequence sequence_ = Sequence::Idle;
    Operation operation_ = Operation::None;
    uint64_t deadline_ = 0;
    uint64_t remaining_ = 0;
    uint64_t eraseRemaining_ = 0;
    bool eraseSuspended_ = false;
    uint64_t suspendAt_ = 0;
    uint64_t eraseWindow_ = 0;
    bool storagePending_ = false, finishError_ = false;
    bool suspended_ = false;
    bool toggle_ = false;
    uint8_t status_ = 0;
    uint8_t count_ = 0;
    uint8_t received_ = 0;
    uint8_t pageSize_ = 0;
    uint32_t commandSector_ = 0;
    uint32_t programBase_ = 0;
    uint32_t valid_ = 0;
    std::array<uint8_t, 32> data_{};
    std::array<uint8_t, 17> protected_{};
    std::array<uint8_t, 17> erasePending_{};
};
