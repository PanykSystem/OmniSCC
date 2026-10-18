#include "KUCRuntime.hxx"
#include "KUCSaveStorage.hxx"
#include "KUCFlashPolicy.hxx"
#include "KUCFlashSPI.hxx"
#ifdef PICO_ON_DEVICE
#include "pico/bootrom.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/io_qspi.h"
#include "hardware/regs/io_qspi.h"
#include "hardware/regs/ssi.h"
#endif
#include "VolumeAccess.h"
extern "C" {
#include "FlashAdapter.h"
}

#ifdef PICO_ON_DEVICE
void KUC_RAM(KUCFlashSPI::Begin)()
{
    // Boot-ROM functions configure SSI for direct 8-bit command transfers.
    auto connect = reinterpret_cast<rom_connect_internal_flash_fn>(
        rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH));
    auto exit = reinterpret_cast<rom_flash_exit_xip_fn>(
        rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP));
    connect();
    exit();
}

void KUC_RAM(KUCFlashSPI::Transfer)(const uint8_t* tx, uint8_t* rx, size_t count)
{
    // Match the SDK's RP2040 direct-transfer FIFO discipline, but keep XIP
    // disabled across the ENTIRE program operation rather than one command.
    hw_write_masked(&io_qspi_hw->io[1].ctrl,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS);
    size_t sent = 0, received = 0;
    while (sent < count || received < count) {
        const uint32_t status = ssi_hw->sr;
        if ((status & SSI_SR_TFNF_BITS) && sent < count && sent - received < 14) {
            ssi_hw->dr0 = tx[sent++];
        }
        if ((status & SSI_SR_RFNE_BITS) && received < count) rx[received++] = uint8_t(ssi_hw->dr0);
    }
    hw_write_masked(&io_qspi_hw->io[1].ctrl,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_HIGH << IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS);
}

void KUC_RAM(KUCFlashSPI::End)()
{
    // WIP is clear before ANY XIP restoration. First establish generic command
    // XIP, then let the SDK restore the board's copied boot2 quad-read setup.
    auto flush = reinterpret_cast<rom_flash_flush_cache_fn>(
        rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE));
    auto enter = reinterpret_cast<rom_flash_enter_cmd_xip_fn>(
        rom_func_lookup_inline(ROM_FUNC_FLASH_ENTER_CMD_XIP));
    flush();
    enter();
    const uint8_t tx[2] = {0x05, 0};
    uint8_t rx[2];
    flash_do_cmd(tx, rx, sizeof(tx));
}
#endif

bool KUCSaveStorage::SupportedChip(bool allowUnknown)
{
    // Read identity while the menu owns flash. The explicit unknown-profile
    // option changes admission only; it never changes the SPI command protocol.
    const uint8_t tx[4] = {0x9f, 0, 0, 0};
    uint8_t rx[4];
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_do_cmd(tx, rx, sizeof(tx));
    restore_interrupts(interrupts);
    return KUCFlashCompatible(rx[1], rx[2], rx[3], allowUnknown);
}

bool KUC_RAM(KUCSaveStorage::Address)(uint32_t offset, uint32_t& physical) const
{
    if (!pages_ || offset >= capacity_) return false;
    physical = uint32_t(pages_[offset >> 9]) * 512 + (offset & 511);
    return physical >= FS_BASE && physical < PICO_FLASH_SIZE_BYTES;
}

bool KUC_RAM(KUCSaveStorage::Read)(void* context, uint32_t offset,
                                                             uint8_t* data, uint32_t size)
{
    const auto& self = *static_cast<KUCSaveStorage*>(context);
    if (!data || offset > self.capacity_ || size > self.capacity_ - offset) return false;
    while (size) {
        uint32_t physical;
        if (!self.Address(offset, physical)) return false;
        uint32_t count = 512 - (offset & 511);
        if (count > size) count = size;
        const auto* source = reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + physical));
        for (uint32_t i = 0; i < count; ++i) data[i] = source[i];
        offset += count;
        data += count;
        size -= count;
    }
    return true;
}

void KUC_RAM(KUCSaveStorage::ProgramSpan)(uint32_t address,
                                                              const uint8_t* data, unsigned size)
{
    uint8_t tx[100], rx[100];
    KUCFlashSPI::Begin();
    tx[0] = 0x06; // Write enable.
    KUCFlashSPI::Transfer(tx, rx, 1);
    tx[0] = 0x02;
    tx[1] = uint8_t(address >> 16);
    tx[2] = uint8_t(address >> 8);
    tx[3] = uint8_t(address);
    for (unsigned i = 0; i < size; ++i) tx[4 + i] = data[i];
    KUCFlashSPI::Transfer(tx, rx, size + 4);
    // ProgramSpan and every caller remain in SRAM until WIP clears. A timeout
    // cannot safely return into XIP while the chip is still programming.
    do {
        tx[0] = 0x05;
        tx[1] = 0;
        KUCFlashSPI::Transfer(tx, rx, 2);
    } while (rx[1] & 1);
    KUCFlashSPI::End();
}

bool KUC_RAM(KUCSaveStorage::ProgramPage)(void* context, uint32_t offset,
                                                                         const uint8_t* data)
{
    auto& self = *static_cast<KUCSaveStorage*>(context);
    const VolumeOwner owner = volume_owner();
    if ((owner != VOLUME_MENU && owner != VOLUME_ROM) || !data
        || offset % 256 || offset > self.capacity_ || self.capacity_ - offset < 256) return false;
    uint32_t physical;
    if (!self.Address(offset, physical)) return false;
    const auto* existing = reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + physical));
    for (unsigned i = 0; i < 256; ++i) {
        if (existing[i] != 0xff || (i >= 96 && i < 252 && data[i] != 0xff)) return false;
    }
    if (owner == VOLUME_ROM) {
        // Queue only; never hold the current MSX bus cycle for the entire
        // two-page transaction. Service() programs one short span later.
        if (self.queuedCount_ == 2 || self.queuedPage_ || self.cursor_) return false;
        const unsigned slot = self.queuedCount_;
        if (slot && offset == self.queuedAddress_[0]) return false;
        for (unsigned i = 0; i < 256; ++i) self.queued_[slot * 256 + i] = data[i];
        self.queuedAddress_[slot] = offset;
        self.queuedOffset_[slot] = uint16_t(slot * 256);
        self.queuedSize_[slot] = 256;
        self.queuedBytes_ = uint16_t((slot + 1) * 256);
        ++self.queuedCount_;
        return true;
    }
    // Journal pages use at most a 96-byte prefix and a four-byte checksum.
    // BY25Q128ES: tBP1 <=100 us, tBP2 <=9 us. Four short commands per journal
    // transaction bound programming to <=1444 us, excluding SPI/verification.
    // Sending 256 bytes would instead permit tPP=2.4 ms and is not acceptable.
    unsigned prefix = 96;
    while (prefix && data[prefix - 1] == 0xff) --prefix;
    const uint32_t interrupts = save_and_disable_interrupts();
    if (prefix) ProgramSpan(physical, data, prefix);
    bool checksum = false;
    for (unsigned i = 252; i < 256; ++i) checksum |= data[i] != 0xff;
    if (checksum) ProgramSpan(physical + 252, data + 252, 4);
    restore_interrupts(interrupts);
    for (unsigned i = 0; i < 256; ++i) if (existing[i] != data[i]) return false;
    return true;
}


// Direct NOR writes accept existing data only for1->0 transitions. Allocation
// and erase are menu-only; this callback cannot change the FAT or page map.
bool KUC_RAM(KUCSaveStorage::Program)(void* context, uint32_t offset,
                                     const uint8_t* data, uint32_t size)
{
    auto& self = *static_cast<KUCSaveStorage*>(context);
    const VolumeOwner owner = volume_owner();
    if ((owner != VOLUME_MENU && owner != VOLUME_ROM) || !data || !size
        || size > 512 || offset > self.capacity_ || size > self.capacity_ - offset)
        return false;
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t physical;
        if (!self.Address(offset + i, physical)) return false;
        const uint8_t previous = *reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + physical));
        if ((previous & data[i]) != data[i]) return false;
    }
    if (owner == VOLUME_ROM) {
        // Existing sectors usually change a single byte. Finish one bounded PP
        // in the current selected cycle instead of adding scheduler latency.
        // Allocation transactions already have queued data, so their commit
        // cannot overtake earlier spans through this fast path.
        if (!self.queuedCount_ && !self.queuedPage_ && !self.cursor_) {
            uint32_t first = 0, last = size;
            while (first < size) {
                uint32_t physical;
                if (!self.Address(offset + first, physical)) return false;
                if (*reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + physical)) != data[first]) break;
                ++first;
            }
            if (first == size) return true;
            while (last > first) {
                uint32_t physical;
                if (!self.Address(offset + last - 1, physical)) return false;
                if (*reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + physical)) != data[last - 1]) break;
                --last;
            }
            uint32_t physical;
            if (!self.Address(offset + first, physical)) return false;
            const uint32_t count = last - first;
            if (count <= 16 && count <= 256 - (physical & 255)) {
                const uint32_t interrupts = save_and_disable_interrupts();
                ProgramSpan(physical, data + first, count);
                restore_interrupts(interrupts);
                const auto* actual = reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + physical));
                for (uint32_t i = 0; i < count; ++i) if (actual[i] != data[first + i]) return false;
                return true;
            }
        }
        if (self.queuedPage_ || self.cursor_ || self.queuedCount_ == 3
            || size > sizeof(self.queued_) - self.queuedBytes_) return false;
        // The core's payload/descriptor/commit ranges must never overlap.
        for (unsigned n = 0; n < self.queuedCount_; ++n) {
            if (offset < self.queuedAddress_[n] + self.queuedSize_[n]
                && self.queuedAddress_[n] < offset + size) return false;
        }
        const unsigned n = self.queuedCount_++;
        self.queuedAddress_[n] = offset;
        self.queuedOffset_[n] = self.queuedBytes_;
        self.queuedSize_[n] = uint16_t(size);
        for (uint32_t i = 0; i < size; ++i) self.queued_[self.queuedBytes_ + i] = data[i];
        self.queuedBytes_ += uint16_t(size);
        return true;
    }
    // Menu-time migration has no selected-cycle scheduler. Still use the same
    // bounded SPI primitive and never cross physical/page-program boundaries.
    uint32_t cursor = 0;
    while (cursor < size) {
        uint32_t physical;
        if (!self.Address(offset + cursor, physical)) return false;
        const auto* actual = reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + physical));
        if (*actual == data[cursor]) { ++cursor; continue; }
        uint32_t count = size - cursor;
        if (count > 16) count = 16;
        if (count > 256 - (physical & 255)) count = 256 - (physical & 255);
        const uint32_t interrupts = save_and_disable_interrupts();
        ProgramSpan(physical, data + cursor, count);
        restore_interrupts(interrupts);
        for (uint32_t i = 0; i < count; ++i) if (actual[i] != data[cursor + i]) return false;
        cursor += count;
    }
    return true;
}

bool KUC_RAM(KUCSaveStorage::Pending)(void* context)
{
    const auto& self = *static_cast<KUCSaveStorage*>(context);
    return self.queuedPage_ < self.queuedCount_;
}

bool KUC_RAM(KUCSaveStorage::Service)(void* context)
{
    auto& self = *static_cast<KUCSaveStorage*>(context);
    if (volume_owner() != VOLUME_ROM || !Pending(context)) return false;
    while (self.queuedPage_ < self.queuedCount_) {
        const unsigned page = self.queuedPage_;
        const uint8_t* data = self.queued_ + self.queuedOffset_[page];
        const unsigned size = self.queuedSize_[page];
        uint32_t physical = 0;
        // Skip unchanged bytes, including erased padding, without programming.
        while (self.cursor_ < size) {
            if (!self.Address(self.queuedAddress_[page] + self.cursor_, physical)) return false;
            if (*reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + physical)) != data[self.cursor_]) break;
            ++self.cursor_;
        }
        if (self.cursor_ == size) {
            self.cursor_ = 0;
            ++self.queuedPage_;
            continue;
        }
        const unsigned start = self.cursor_;
        unsigned count = size - start;
        if (count > 16) count = 16;
        if (count > 256 - (physical & 255)) count = 256 - (physical & 255);
        const auto* actual = reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + physical));
        while (count > 1 && actual[count - 1] == data[start + count - 1]) --count;
        const uint32_t interrupts = save_and_disable_interrupts();
        ProgramSpan(physical, data + start, count);
        restore_interrupts(interrupts);
        for (unsigned i = 0; i < count; ++i) if (actual[i] != data[start + i]) return false;
        self.cursor_ = uint16_t(start + count);
        // Advance unchanged suffixes, but never issue a second PP this cycle.
        while (self.cursor_ < size) {
            uint32_t next;
            if (!self.Address(self.queuedAddress_[page] + self.cursor_, next)) return false;
            if (*reinterpret_cast<const volatile uint8_t*>(uintptr_t(XIP_BASE + next)) != data[self.cursor_]) break;
            ++self.cursor_;
        }
        if (self.cursor_ == size) {
            self.cursor_ = 0;
            ++self.queuedPage_;
        }
        if (self.queuedPage_ == self.queuedCount_) {
            self.queuedCount_ = self.queuedPage_ = 0;
            self.queuedBytes_ = 0;
        }
        return true;
    }
    self.queuedCount_ = self.queuedPage_ = 0;
    self.queuedBytes_ = 0;
    return true;
}
