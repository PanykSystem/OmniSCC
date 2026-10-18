#include "KUCMapper.hxx"

// These functions participate in cartridge bus cycles and must remain in SRAM.
#include "KUCRuntime.hxx"

void KUC_RAM(KUCMapper::Reset)()
{
    control_ = offset_ = sccMode_ = 0;
    for (unsigned i = 0; i < 4; ++i) banks_[i] = uint8_t(i);
}

uint32_t KUC_RAM(KUCMapper::FlashAddress)(uint16_t address) const
{
    if (address < 0x4000 || address >= 0xc000) return Unmapped;
    const unsigned window = (address - 0x4000) >> 13;
    const uint8_t bank = uint8_t(banks_[window] + offset_);
    return (uint32_t(control_ & 0xc0) << 15)
         | (uint32_t(bank) << 13) | (address & 0x1fff);
}

bool KUC_RAM(KUCMapper::IsSCC)(uint16_t address) const
{
    // Unlike a discrete SCC, KUC does not decode mirrors with A8 set.
    if ((sccMode_ & 0x10) || (address & 0x0100)) return false;
    if (SCCPlus()) {
        return (banks_[3] & 0x80) && address >= 0xb800 && address < 0xbffe;
    }
    return (banks_[2] & 0x3f) == 0x3f
        && address >= 0x9800 && address < 0x9ffe;
}

bool KUC_RAM(KUCMapper::IsDAC)(uint16_t address) const
{
    return (control_ & 0x08) && address >= 0x4000
        && address < 0x6000 && !(address & 0x10);
}

void KUC_RAM(KUCMapper::WriteRegisters)(uint16_t address, uint8_t value)
{
    if (address < 0x4000 || address >= 0xc000 || IsSCC(address)) return;

    // Control writes take effect before bank-register and mode decoding. The
    // control lock covers BOTH 7FFE and 7FFF and can only be undone by reset.
    if (!(control_ & 0x04)) {
        if (address == 0x7fff) control_ = value;
        else if (address == 0x7ffe) offset_ = value;
    }
    if (control_ & 0x02) return;

    const unsigned window = (address - 0x4000) >> 13;
    if (!(control_ & 0x20)) {
        if ((address & 0x1800) == 0x1000) banks_[window] = value;
    } else if (!(window == 0 && (control_ & 0x08))) {
        if (address >= 0x6000 || (address >= 0x5000 && address < 0x5800)) {
            banks_[window] = value;
        }
    }
    if ((address & 0xfffe) == 0xbffe) sccMode_ = value;
}
