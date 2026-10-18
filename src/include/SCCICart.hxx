#pragma once
#include "Cartridge.hxx"
#include "SCCIMapper.hxx"
#include "SCCSound.hxx"

// Uses the ordinary ROM staging buffer as volatile 128 KiB cartridge RAM.
// No additional RAM allocation, filesystem activity or flash writes in game.
class CSCCICartridge : public CCartridge {
public:
    CSCCICartridge(COmniSCC& manager, CSCCSound& sound, const RomStorage& storage, bool enhanced)
        : CCartridge(manager, storage), sound_(sound) {
        mapper_.Reset(enhanced);
        sound_.reset();
    }
    bool Execute() override;
private:
    SCCIMapper mapper_;
    CSCCSound& sound_;
};
