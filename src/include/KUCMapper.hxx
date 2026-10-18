#pragma once

#include <cstdint>
#include <array>

// KUC's register decoder is independent of the bus and the flash command engine.
// The caller calculates the flash address BEFORE applying WriteRegisters(): one
// bus write can reach both the old flash address and a mapper register.
class KUCMapper {
public:
    static constexpr uint32_t Unmapped = 0xffffffffu;

    void Reset();
    uint32_t FlashAddress(uint16_t address) const;
    bool IsSCC(uint16_t address) const;
    bool IsDAC(uint16_t address) const;
    void WriteRegisters(uint16_t address, uint8_t value);

    uint8_t Control() const { return control_; }
    uint8_t Offset() const { return offset_; }
    bool FlashWriteEnabled() const { return (control_ & 0x10) != 0; }
    bool SCCPlus() const { return (sccMode_ & 0x20) != 0; }

    template<class Archive> void serialize(Archive& ar, unsigned) {
        ar.serialize("control", control_, "offset", offset_, "sccMode", sccMode_, "banks", banks_);
    }

private:
    uint8_t control_ = 0;
    uint8_t offset_ = 0;
    uint8_t sccMode_ = 0;
    std::array<uint8_t, 4> banks_ = {0, 1, 2, 3};
};
