#include "KUCFlash.hxx"

#include "KUCRuntime.hxx"

// M29W640GB has eight 8 KiB bottom sectors, followed by 127 64 KiB sectors.
uint32_t KUC_RAM(KUCFlash::SectorStart)(uint32_t address)
{
    return address & (address < 0x10000 ? 0x7fe000u : 0x7f0000u);
}

uint32_t KUC_RAM(KUCFlash::SectorSize)(uint32_t address)
{
    return address < 0x10000 ? 0x2000 : 0x10000;
}

unsigned KUC_RAM(KUCFlash::SectorIndex)(uint32_t address) const
{
    return address < 0x10000 ? address >> 13 : 7 + (address >> 16);
}

bool KUC_RAM(KUCFlash::Protected)(uint32_t address) const
{
    const unsigned index = SectorIndex(address);
    return (protected_[index >> 3] & (1u << (index & 7))) != 0;
}

void KUCFlash::SetProtected(uint32_t address, bool protect)
{
    if (address >= 0x800000) return;
    const unsigned index = SectorIndex(address);
    const uint8_t bit = uint8_t(1u << (index & 7));
    if (protect) protected_[index >> 3] |= bit;
    else protected_[index >> 3] &= uint8_t(~bit);
}

void KUC_RAM(KUCFlash::MarkErase)(uint32_t address)
{
    if (Protected(address)) return;
    const unsigned index = SectorIndex(address);
    erasePending_[index >> 3] |= uint8_t(1u << (index & 7));
}

bool KUC_RAM(KUCFlash::PendingErase)(uint32_t address) const
{
    const unsigned index = SectorIndex(address);
    return (erasePending_[index >> 3] & (1u << (index & 7))) != 0;
}

void KUC_RAM(KUCFlash::ClearSequence)()
{
    sequence_ = Sequence::Idle;
    valid_ = 0;
    count_ = received_ = 0;
}

void KUC_RAM(KUCFlash::Reset)()
{
    // A submitted durable transaction must drain before it can report ready.
    // Dropping its tracking here would orphan the backend's partially written
    // slot. Ordinary transient commands can still be canceled before submission.
    if (storagePending_) return;
    mode_ = Mode::Array;
    operation_ = Operation::None;
    storagePending_ = finishError_ = false;
    suspended_ = eraseSuspended_ = false;
    suspendAt_ = 0;
    status_ = 0;
    for (auto& byte : erasePending_) byte = 0;
    ClearSequence();
}

void KUC_RAM(KUCFlash::Fail)(bool bufferAbort)
{
    mode_ = Mode::Error;
    operation_ = Operation::None;
    suspended_ = false;
    suspendAt_ = 0;
    status_ |= uint8_t(0x20 | (bufferAbort ? 0x02 : 0));
    ClearSequence();
}

void KUC_RAM(KUCFlash::StartProgram)(uint64_t now)
{
    if (Protected(programBase_) || PendingErase(programBase_)) {
        ClearSequence();
        return;
    }
    operation_ = Operation::Program;
    suspendAt_ = 0;
    unsigned written = 0;
    for (unsigned i = 0; i < pageSize_; ++i) written += (valid_ >> i) & 1;
    deadline_ = now + 10 + (written > 1 ? (11 * (written - 1) + 1) / 2 : 0);
    status_ &= 0x80;
    suspended_ = false;
    sequence_ = Sequence::Idle;
}

void KUC_RAM(KUCFlash::CompleteMutation)(uint64_t now, bool error)
{
    if (error) { Fail(); return; }
    if (operation_ != Operation::Program) {
        for (const auto byte : erasePending_) {
            if (byte) { deadline_ = now + storage_.eraseMicroseconds; return; }
        }
    }
    operation_ = Operation::None;
    status_ = 0;
    suspendAt_ = 0;
    ClearSequence();
}

void KUC_RAM(KUCFlash::Advance)(uint64_t now)
{
    if (operation_ == Operation::None) return;
    if (storagePending_) {
        if (now < deadline_) return;
        // Persist ONE short span, then allow at least 0.25 ms of Z80 execution
        // before the next /WAIT hold. The deadline is relative to entry, and
        // the physical span itself takes at most about 0.24 ms plus overhead.
        if (!storage_.service(storage_.context)) {
            storagePending_ = false;
            Fail();
            return;
        }
        if (storage_.pending(storage_.context)) {
            deadline_ = now + 600;
            return;
        }
        storagePending_ = false;
        CompleteMutation(now, finishError_);
        return;
    }
    if (suspendAt_ && now >= suspendAt_ && suspendAt_ < deadline_) {
        remaining_ = deadline_ - suspendAt_;
        if (operation_ == Operation::SectorErase) {
            eraseSuspended_ = true;
            eraseRemaining_ = remaining_;
            operation_ = Operation::None;
        } else suspended_ = true;
        suspendAt_ = 0;
    }
    if (operation_ == Operation::None || suspended_ || now < deadline_) return;

    bool bitRaise = false;
    if (operation_ == Operation::Program) {
        uint8_t programmed[32] = {};
        for (unsigned i = 0; i < pageSize_; ++i) {
            const uint8_t old = storage_.read(storage_.context, programBase_ + i);
            programmed[i] = old;
            if (!bitRaise && (valid_ & (1u << i))) {
                programmed[i] &= data_[i];
                bitRaise |= programmed[i] != data_[i];
            }
        }
        if (!storage_.program(storage_.context, programBase_, programmed, pageSize_)) {
            Fail();
            return;
        }
    } else {
        for (uint32_t address = 0; address < 0x800000; address += SectorSize(address)) {
            if (!PendingErase(address)) continue;
            if (!storage_.erase(storage_.context, address, SectorSize(address))) {
                Fail();
                return;
            }
            const unsigned index = SectorIndex(address);
            erasePending_[index >> 3] &= uint8_t(~(1u << (index & 7)));
            break;
        }
    }
    if (storage_.pending && storage_.service && storage_.pending(storage_.context)) {
        storagePending_ = true;
        finishError_ = bitRaise;
        deadline_ = now + 600;
        return;
    }
    CompleteMutation(now, bitRaise);
}

uint8_t KUC_RAM(KUCFlash::Query)(uint32_t address) const
{
    // CFI is word-addressed, even when commands/data travel over an 8-bit bus.
    const uint32_t word = (address >> 1) & 0xff;
    if (address & 1) return 0;
    switch (word) {
    case 0x10: return 'Q';
    case 0x11: return 'R';
    case 0x12: return 'Y';
    case 0x13: return 2;       // AMD/Fujitsu standard command set.
    case 0x15: return 0x40;    // Primary extended query table.
    case 0x1b: return 0x27;    // 2.7 V minimum supply.
    case 0x1c: return 0x36;    // 3.6 V maximum supply.
    case 0x1d: return 0xb5;
    case 0x1e: return 0xc5;
    case 0x1f: return 4;       // 16 us typical single program.
    case 0x21: return 10;      // 1024 ms typical sector erase.
    case 0x23: return 4;       // Maximum program multiplier: 16.
    case 0x24: return 3;
    case 0x25: return 3;
    case 0x26: return 1;
    case 0x27: return 23;      // 2^23 bytes.
    case 0x28: return 2;       // x8/x16 interface.
    case 0x2a: return 5;       // 32-byte write buffer.
    case 0x2c: return 2;       // Two erase regions.
    case 0x2d: return 7;       // Eight small sectors, count minus one.
    case 0x2f: return 0x20;    // 8192 / 256.
    case 0x31: return 126;     // 127 large sectors.
    case 0x34: return 1;       // 65536 / 256 = 0100h.
    case 0x40: return 'P';
    case 0x41: return 'R';
    case 0x42: return 'I';
    case 0x43: return '1';
    case 0x44: return '3';
    case 0x46: return 2;       // Erase suspend supported.
    case 0x47: return 4;
    case 0x48: return 1;
    case 0x49: return 4;
    case 0x4b: return 1;
    case 0x4c: return 1;
    case 0x4d: return 0xb5;
    case 0x4e: return 0xc5;
    case 0x4f: return 2;       // Bottom boot layout.
    case 0x50: return 1;       // Program suspend supported.
    default: return 0;
    }
}

uint8_t KUC_RAM(KUCFlash::Read)(uint32_t address, uint64_t now)
{
    if (address >= 0x800000) return 0xff;
    Advance(now);
    if (eraseSuspended_ && mode_ == Mode::Array && PendingErase(address)) {
        toggle_ = !toggle_;
        return uint8_t(0x80 | (toggle_ ? 4 : 0));
    }
    if (operation_ != Operation::None) {
        if (!suspended_ || PendingErase(address)
            || (operation_ == Operation::Program && (address & ~(uint32_t(pageSize_) - 1)) == programBase_)) {
            toggle_ = !toggle_;
            const uint8_t timer = operation_ == Operation::SectorErase && now >= eraseWindow_ ? 8 : 0;
            if (suspended_ && operation_ == Operation::SectorErase) {
                return uint8_t(0x80 | (toggle_ ? 0x04 : 0));
            }
            return uint8_t(status_ | timer | (toggle_ ? 0x40 : 0));
        }
    }
    if (mode_ == Mode::Error) {
        toggle_ = !toggle_;
        return status_ | (toggle_ ? 0x40 : 0);
    }
    if (mode_ == Mode::Query) return Query(address);
    if (mode_ == Mode::Identify) {
        if (address & 1) return 0;
        switch ((address >> 1) & 0x7f) {
        case 0: return 0x20;  // STMicroelectronics/Numonyx manufacturer.
        case 1: return 0x7e;
        case 2: return Protected(address) ? 1 : 0;
        case 3: return 0x08;
        case 0x0e: return 0x10;
        case 0x0f: return 0;
        default: return 0;
        }
    }
    return storage_.read(storage_.context, address);
}

void KUC_RAM(KUCFlash::Write)(uint32_t address, uint8_t value, uint64_t now)
{
    if (address >= 0x800000) return;
    Advance(now);
    if (storagePending_) return;
    if (operation_ != Operation::None) {
        if (suspended_) {
            if (value == 0x30) {
                suspended_ = false;
                deadline_ = now + remaining_;
            }
        } else if (value == 0xb0 && operation_ != Operation::ChipErase) {
            if (!suspendAt_) suspendAt_ = now + 4;
        } else if (value == 0x30 && operation_ == Operation::SectorErase && now < eraseWindow_) {
            MarkErase(address);
            eraseWindow_ = now + 50;
            deadline_ = eraseWindow_ + storage_.eraseMicroseconds;
        }
        return;
    }

    // F0 is legal program data. Only interpret it as reset outside data phases.
    if (value == 0xf0 && sequence_ != Sequence::ProgramData
        && sequence_ != Sequence::BufferData) {
        if (eraseSuspended_) {
            mode_ = Mode::Array;
            status_ = 0;
            ClearSequence();
        } else Reset();
        return;
    }
    if (eraseSuspended_ && sequence_ == Sequence::Idle && value == 0x30) {
        eraseSuspended_ = false;
        operation_ = Operation::SectorErase;
        deadline_ = now + eraseRemaining_;
        return;
    }
    if (mode_ == Mode::Error) return;
    const uint32_t native = (address >> 1) & 0x7ff;
    if (mode_ != Mode::Array) {
        // Identification permits entering CFI, but neither mode permits array
        // mutations. A software reset above is required to return to the array.
        if (mode_ == Mode::Identify && native == 0x55 && value == 0x98) {
            mode_ = Mode::Query;
        }
        return;
    }
    switch (sequence_) {
    case Sequence::Idle:
        if (native == 0x555 && value == 0xaa) sequence_ = Sequence::Unlock2;
        else if (native == 0x55 && value == 0x98) mode_ = Mode::Query;
        else if (native == 0x555 && (value == 0x50 || value == 0x56)) {
            count_ = value == 0x50 ? 2 : 4;
            received_ = 0;
            valid_ = 0;
            pageSize_ = 8;
            sequence_ = Sequence::ProgramData;
        }
        break;
    case Sequence::Unlock2:
        sequence_ = native == 0x2aa && value == 0x55 ? Sequence::Command : Sequence::Idle;
        break;
    case Sequence::Command:
        sequence_ = Sequence::Idle;
        if (value == 0x25) {
            commandSector_ = SectorStart(address);
            sequence_ = Sequence::BufferCount;
        } else if (native == 0x555) {
            if (value == 0x90) mode_ = Mode::Identify;
            else if (value == 0x80 && !eraseSuspended_) sequence_ = Sequence::EraseUnlock1;
            else if (value == 0xa0) {
                count_ = 1;
                received_ = 0;
                valid_ = 0;
                pageSize_ = 8;
                sequence_ = Sequence::ProgramData;
            }
        }
        break;
    case Sequence::EraseUnlock1:
        sequence_ = native == 0x555 && value == 0xaa ? Sequence::EraseUnlock2 : Sequence::Idle;
        break;
    case Sequence::EraseUnlock2:
        sequence_ = native == 0x2aa && value == 0x55 ? Sequence::EraseConfirm : Sequence::Idle;
        break;
    case Sequence::EraseConfirm:
        ClearSequence();
        if (value == 0x30 || value == 0x10) {
            for (auto& byte : erasePending_) byte = 0;
            if (value == 0x30) MarkErase(address);
            else for (uint32_t sector = 0; sector < 0x800000; sector += SectorSize(sector)) MarkErase(sector);
            operation_ = value == 0x30 ? Operation::SectorErase : Operation::ChipErase;
            eraseWindow_ = now + 50;
            deadline_ = eraseWindow_ + storage_.eraseMicroseconds;
            status_ = 0;
        }
        break;
    case Sequence::BufferCount:
        if (value >= 32 || SectorStart(address) != commandSector_) Fail(true);
        else {
            count_ = value + 1;
            received_ = 0;
            pageSize_ = 32;
            valid_ = 0;
            sequence_ = Sequence::BufferData;
        }
        break;
    case Sequence::ProgramData:
    case Sequence::BufferData: {
        const bool buffered = sequence_ == Sequence::BufferData;
        if (received_ == 0 || !buffered) programBase_ = address & ~(uint32_t(pageSize_) - 1);
        if (buffered && ((address & ~31u) != programBase_ || SectorStart(address) != commandSector_)) {
            Fail(true);
            break;
        }
        const unsigned offset = address & (pageSize_ - 1);
        const uint32_t bit = 1u << offset;
        if (!buffered && (valid_ & bit)) data_[offset] &= value;
        else data_[offset] = value;
        valid_ |= bit;
        status_ = uint8_t(~value & 0x80);
        if (++received_ == count_) {
            if (buffered) sequence_ = Sequence::BufferConfirm;
            else StartProgram(now);
        }
        break;
    }
    case Sequence::BufferConfirm:
        if (value != 0x29 || SectorStart(address) != commandSector_) Fail(true);
        else StartProgram(now);
        break;
    }
}
