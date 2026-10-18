#pragma once

#include "Cartridge.hxx"
#include "KUCFlash.hxx"
#include "KUCMapper.hxx"
#include "KUCSaveStorage.hxx"
#include "SCCSound.hxx"

class CKUCCartridge final : public CCartridge {
public:
    CKUCCartridge(COmniSCC& manager, CSCCSound& sound, const RomStorage& rom,
                  const uint16_t* savePages, uint32_t saveCapacity,
                  uint16_t* index, const uint8_t digest[32]);
    bool Ready() const { return ready_; }
    bool Execute() override;

private:
    static uint8_t ReadFlash(void* context, uint32_t address);
    static uint8_t ReadOriginal(void* context, uint32_t address);
    static bool ProgramFlash(void* context, uint32_t address, const uint8_t* data, uint32_t size);
    static bool SavePending(void* context);
    static bool ServiceSave(void* context);
    static bool EraseFlash(void* context, uint32_t address, uint32_t size);

    bool SCCVisible(uint16_t address) const {
        return address >= sccBegin_ && address < sccEnd_ && !(address & 0x100);
    }

    CSCCSound& sound_;
    KUCMapper mapper_;
    KUCSaveStorage saveStorage_;
    KUCSectorSave save_;
    KUCFlash flash_;
    const uint16_t* index_;
    uint32_t bankBase_[4] = {0, 0x2000, 0x4000, 0x6000};
    uint16_t sccBegin_ = 0, sccEnd_ = 0;
    bool ready_;
};
