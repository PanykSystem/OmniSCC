/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

 #include "KonamiSCCCart.hxx"
#include <string.h>


CKonamiSCCCartridge::CKonamiSCCCartridge(COmniSCC& multiCart, CSCCSound& scc, uint8_t* data, uint32_t sizeMask): CCartridge(multiCart, data, sizeMask), _scc(scc)
{
    // Power-on state for the chip. This is the only place the SCC is reset:
    // the mode-select register must not disturb it (see Execute()).
    _scc.reset();
}

CKonamiSCCCartridge::CKonamiSCCCartridge(COmniSCC& multiCart, CSCCSound& scc, const RomStorage& storage): CCartridge(multiCart,storage), _scc(scc) {_scc.reset();}

CKonamiSCCCartridge::~CKonamiSCCCartridge() 
{

}

//#pragma GCC push_options
//#pragma GCC optimize("O3")

/**
 * Konami SCC mapper emulation loop.
 *
 * The Konami SCC mapper divides the 0x4000-0xBFFF cartridge region into four
 * 8 KB pages:
 *   page 0 : 0x4000-0x5FFF  (bank register at 0x5000)
 *   page 1 : 0x6000-0x7FFF  (bank register at 0x7000)
 *   page 2 : 0x8000-0x9FFF  (bank register at 0x9000, also SCC mode select)
 *   page 3 : 0xA000-0xBFFF  (bank register at 0xB000)
 *
 * Bank switching:
 *   Writes to 0x5000/0x7000/0x9000/0xB000 select a 8 KB ROM bank using the
 *   full 8-bit written value (up to 2 MiB, extending the original 512 KiB mapper).
 *
 * SCC:
 *   Writing a value whose low 6 bits equal 0x3F to 0x9000-0x97FF enables
 *   the SCC. Once enabled, reads and
 *   writes in 0x9800-0x9FFF access SCC registers and take priority over the
 *   mapper bank register.
 *
 * This function runs from SRAM (__not_in_flash_func) to meet Z80 timing.
 */
bool __not_in_flash_func(CKonamiSCCCartridge::Execute())
{
    return _storage.kind == RomStorageKind::Ram ? ExecuteStorage<RomStorageKind::Ram>() : ExecuteStorage<RomStorageKind::Mapped>();
}

template<RomStorageKind Kind>
bool __attribute__((noinline, section(".time_critical.KonamiSCCCart_storage"))) CKonamiSCCCartridge::ExecuteStorage()
{
    uint32_t const irq_state = save_and_disable_interrupts();



    uint32_t pins;
    bool sccEnabled = false;
    // Byte offsets, not pointers: the mask is applied at the read, so a bogus
    // bank register can never index outside the ROM.
    uint32_t pages[4];
    pages[0] = 0x0000;
    pages[1] = 0x2000;
    pages[2] = 0x4000;
    pages[3] = 0x6000;

    while (1) {
        // Wait for the cartridge slot to be selected.
        while (((pins = gpio_get_all()) & (SLTSL_GPIO_MASK)) == SLTSL_GPIO_MASK);

        // Stretch the Z80 cycle by asserting /WAIT before the T2 sample point.
        forceWait();

        // Hold it. /SLTSL leads /RD and /WR by the best part of a T-state, so
        // the sample that caught the selection usually has no strobe yet.
        // Releasing and re-asserting across that lead chops the line into a
        // pulse train, and the Z80 samples /WAIT on a single clock edge -- which
        // is how that sample lands in a gap and the Z80 reads the bus before the
        // byte is driven. The spin ends when a strobe appears or /SLTSL rises,
        // and every path out of it releases the line. Measured and commented at
        // length in CDiskCartridge::Execute, which has run this way for weeks.
        while ((pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK | WR_GPIO_MASK))
               == (RD_GPIO_MASK | WR_GPIO_MASK)) {
            pins = gpio_get_all();
        }

        if(!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK)))           // /SLTSL + /RD active
        {
            uint32_t addr = ((pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT);
            uint16_t page_addr = addr & 0x1FFF;

            // Map the 16 KB address window to one of the four 8 KB pages.
            // The formula ((addr >> 13) + 2) & 0x3 converts:
            //   0x4000-0x5FFF -> page 0
            //   0x6000-0x7FFF -> page 1
            //   0x8000-0x9FFF -> page 2
            //   0xA000-0xBFFF -> page 3
            uint16_t page = ((addr >> 13) + 2) & 0x3;

            uint8_t data;

            // SCC register reads have priority over ROM reads.
            if (sccEnabled && (addr >= 0x9800) && (addr < 0xA000)) {
                data = _scc.readRegister((uint8_t)addr);
            } else {
                data = _storage.Read<Kind>(pages[page] + page_addr);
            }            

            gpio_put_masked(DATA_GPIO_MASK, (uint32_t)(data));
            SET_DATA_MODE_OUT;
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 1);  // Pico -> MSX
#endif
            releaseWait();
            while (!(gpio_get_all() & RD_GPIO_MASK));
            //for(volatile int cnt = 0; cnt < 5; cnt++);            
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 0);  // MSX -> Pico
#endif
            SET_DATA_MODE_IN;

        } else if (!(pins & (SLTSL_GPIO_MASK | WR_GPIO_MASK))) { // /SLTSL + /WR active
            const uint8_t data = (uint8_t)(pins & DATA_GPIO_MASK);
            const uint32_t addr = (pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;

            if ((addr >= 0x5000) && (addr < 0xC000)) {
                // SCC register writes have priority over mapper writes.
                if (sccEnabled && (addr >= 0x9800) && (addr < 0xA000)) {
                    _scc.enqueueWrite((uint8_t)(addr & 0xFF), data);
                } else {
                    // 0x9000-0x97FF: bank 2 switch AND SCC mode select.
                    //
                    // Only the visibility of the SCC register window changes
                    // here. The chip itself must NOT be reset: this address is
                    // the page-2 bank register, and games swap page 2 between
                    // ROM banks and the SCC window constantly, so resetting on
                    // every 0x3F write wipes the wavetables and clears
                    // enableMask_, which silences the SCC for good.
                    if ((addr & 0xF800) == 0x9000) {
                        sccEnabled = ((data & 0x3F) == 0x3F);
                    }

                    // 0x5000-0x57FF, 0x7000-0x77FF, 0x9000-0x97FF, 0xB000-0xB7FF.
                    // Bit pattern: addresses where bits 12..11 == 01.
                    if ((addr & 0x1800) == 0x1000) {
                        const uint16_t page =
                            ((addr >> 13) + 2) & 0x03;

                        pages[page] = ((uint32_t)data) << 13;
                    }
                }
            }

            releaseWait();
            while (!(gpio_get_all() & WR_GPIO_MASK)) {
            }
        } else {
            // No valid read/write strobe: release /WAIT and wait for the next cycle.
            releaseWait();
        }

    }
    restore_interrupts(irq_state);
    return true;
}
//#pragma GCC pop_options
