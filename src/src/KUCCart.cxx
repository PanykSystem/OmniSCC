#include "KUCRuntime.hxx"
#include "KUCCart.hxx"
#ifdef PICO_ON_DEVICE
#include "hardware/structs/timer.h"
#endif

static uint64_t KUC_RAM(KUCMicroseconds)()
{
#ifdef PICO_ON_DEVICE
    uint32_t high, low;
    do {
        high = timer_hw->timerawh;
        low = timer_hw->timerawl;
    } while (high != timer_hw->timerawh);
    return (uint64_t(high) << 32) | low;
#else
    return time_us_64();
#endif
}

CKUCCartridge::CKUCCartridge(COmniSCC& manager, CSCCSound& sound, const RomStorage& rom,
                           const uint16_t* savePages, uint32_t saveCapacity,
                           uint16_t* index, const uint8_t digest[32])
    : CCartridge(manager, rom), sound_(sound), saveStorage_(savePages, saveCapacity),
      save_(saveStorage_.SectorIO(), index, KUCSectorSave::IndexEntries, ReadOriginal, this),
      flash_({this, ReadFlash, ProgramFlash, EraseFlash, SavePending, ServiceSave, 0}),
      index_(index), ready_(save_.Open(digest, rom.size) == KUCSaveResult::Ok)
{
    if (ready_) sound_.reset();
}

uint8_t KUC_RAM(CKUCCartridge::ReadFlash)(void* context, uint32_t address)
{
    auto& self = *static_cast<CKUCCartridge*>(context);
    uint8_t value;
    if (self.save_.ReadByte(address, value)) return value;
    // KUC exposes the complete chip. A smaller ROM leaves erased space, rather
    // than mirroring through RomStorage's ordinary cartridge size mask.
    return ReadOriginal(context, address);
}

uint8_t KUC_RAM(CKUCCartridge::ReadOriginal)(void* context, uint32_t address)
{
    const auto& self = *static_cast<CKUCCartridge*>(context);
    return address < self._storage.size ? self._storage.ReadAny(address) : 0xff;
}

bool KUC_RAM(CKUCCartridge::ProgramFlash)(void* context, uint32_t address,
                                                            const uint8_t* data, uint32_t size)
{
    auto& self = *static_cast<CKUCCartridge*>(context);
    return self.save_.Program(address, data, size) == KUCSaveResult::Ok;
}

bool KUC_RAM(CKUCCartridge::EraseFlash)(void* context, uint32_t address, uint32_t size)
{
    auto& self = *static_cast<CKUCCartridge*>(context);
    return self.save_.Erase(address, size) == KUCSaveResult::Ok;
}

bool KUC_RAM(CKUCCartridge::SavePending)(void* context)
{
    return static_cast<CKUCCartridge*>(context)->save_.Pending();
}

bool KUC_RAM(CKUCCartridge::ServiceSave)(void* context)
{
    return static_cast<CKUCCartridge*>(context)->save_.Service() == KUCSaveResult::Ok;
}

bool KUC_RAM(CKUCCartridge::Execute)()
{
    if (!ready_) return false;
    const uint32_t interrupts = save_and_disable_interrupts();
    for (;;) {
        uint32_t pins;
        // A background /WAIT can stretch a DRAM cycle in another slot far
        // beyond its active-row timing limit. Only service persistence while
        // this cartridge owns an active bus cycle; status reads advance it.
        while (((pins = gpio_get_all()) & SLTSL_GPIO_MASK) == SLTSL_GPIO_MASK) {}
        forceWait();
        while ((pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK | WR_GPIO_MASK))
               == (RD_GPIO_MASK | WR_GPIO_MASK)) pins = gpio_get_all();
        const uint16_t address = uint16_t((pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT);
        if (!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK))) {
            const uint32_t physical = address >= 0x4000 && address < 0xc000
                ? bankBase_[(address - 0x4000) >> 13] | (address & 0x1fff)
                : KUCMapper::Unmapped;
            uint8_t value = 0xff;
            if (SCCVisible(address)) {
                value = sound_.readKUCRegister(uint8_t(address), mapper_.SCCPlus());
            } else if (physical != KUCMapper::Unmapped) {
                if (flash_.ArrayRead()) {
                    // Most instruction/data fetches are unchanged ROM. Avoid
                    // timer MMIO, callback chains and journal decoding entirely.
                    if (!index_[physical >> 9]) {
                        if (physical < _storage.size) {
                            value = _storage.kind == RomStorageKind::Ram
                                ? _storage.ram[physical]
                                : *reinterpret_cast<const volatile uint8_t*>(
                                    uintptr_t(_storage.pages[physical >> 9]) + (physical & 511));
                        }
                    } else value = ReadFlash(this, physical);
                } else value = flash_.Read(physical, KUCMicroseconds());
            }
            gpio_put_masked(DATA_GPIO_MASK, value);
            SET_DATA_MODE_OUT;
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 1);
#endif
            releaseWait();
            while (!(gpio_get_all() & RD_GPIO_MASK)) {}
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 0);
#endif
            SET_DATA_MODE_IN;
        } else if (!(pins & (SLTSL_GPIO_MASK | WR_GPIO_MASK))) {
            const uint8_t value = uint8_t(pins & DATA_GPIO_MASK);
            if (SCCVisible(address)) {
                // Core 1 keeps draining from SRAM. Do not silently drop a KUC
                // event when a rapid waveform upload fills the queue.
                while (!sound_.enqueueKUCWrite(uint8_t(address), value, mapper_.SCCPlus())) {}
            } else {
                const uint32_t physical = mapper_.FlashAddress(address);
                mapper_.WriteRegisters(address, value);
                for (unsigned window = 0; window < 4; ++window) {
                    bankBase_[window] = mapper_.FlashAddress(uint16_t(0x4000 + window * 0x2000));
                }
                sccBegin_ = sccEnd_ = 0;
                if (mapper_.IsSCC(0x9800)) { sccBegin_ = 0x9800; sccEnd_ = 0x9ffe; }
                else if (mapper_.IsSCC(0xb800)) { sccBegin_ = 0xb800; sccEnd_ = 0xbffe; }
                if (mapper_.IsDAC(address)) while (!sound_.enqueueDAC(value)) {}
                if (physical != KUCMapper::Unmapped && mapper_.FlashWriteEnabled()) {
                    flash_.Write(physical, value, KUCMicroseconds());
                }
            }
            releaseWait();
            while (!(gpio_get_all() & WR_GPIO_MASK)) {}
        } else releaseWait();
    }
    restore_interrupts(interrupts);
    return true;
}
