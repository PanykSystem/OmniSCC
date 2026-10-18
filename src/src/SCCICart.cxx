#include "SCCICart.hxx"

bool __not_in_flash_func(CSCCICartridge::Execute())
{
    // Match the other cartridge loops: a USB/timer IRQ must not interrupt
    // a selected bus cycle or leave WAIT/data direction active mid-handler.
    (void)save_and_disable_interrupts();
    uint8_t* const ram = const_cast<uint8_t*>(_storage.ram);
    while (true) {
        uint32_t pins;
        while (((pins = gpio_get_all()) & SLTSL_GPIO_MASK) == SLTSL_GPIO_MASK) {}
        forceWait();
        while ((pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK | WR_GPIO_MASK)) == (RD_GPIO_MASK | WR_GPIO_MASK)) {
            pins = gpio_get_all();
        }
        const uint16_t address = uint16_t((pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT);
        if (!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK))) {
            if (address >= 0x4000 && address < 0xc000) {
                const uint8_t value = mapper_.IsSound(address)
                    ? sound_.readKUCRegister(uint8_t(address), mapper_.PlusMode())
                    : ram[mapper_.MemoryOffset(address)];
                gpio_put_masked(DATA_GPIO_MASK, uint32_t(value));
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
            } else {
                releaseWait();
                while (!(gpio_get_all() & RD_GPIO_MASK)) {}
            }
        } else if (!(pins & (SLTSL_GPIO_MASK | WR_GPIO_MASK))) {
            const uint8_t value = uint8_t(pins & DATA_GPIO_MASK);
            if (mapper_.Write(address, value, ram)) {
                // Preserve short frequency/enable writes even when the renderer
                // is briefly behind. WAIT remains held until Core 1 has room.
                while (!sound_.enqueueKUCWrite(uint8_t(address), value, mapper_.PlusMode())) {}
            }
            releaseWait();
            while (!(gpio_get_all() & WR_GPIO_MASK)) {}
        } else releaseWait();
    }
}
