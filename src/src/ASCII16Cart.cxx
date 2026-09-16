/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#include "ASCII16Cart.hxx"


CASCII16Cartridge::CASCII16Cartridge(COmniSCC& multiCart, uint8_t* data, uint32_t sizeMask): CCartridge(multiCart, data, sizeMask)
{

}

CASCII16Cartridge::~CASCII16Cartridge() 
{

}

bool __not_in_flash_func(CASCII16Cartridge::Execute())
{
    const uint8_t* mem = _data;
    const uint32_t mask = _mask;
    uint32_t pins;
    // Byte offsets, not pointers: the mask is applied at the read, so a bogus
    // bank register can never index outside the ROM.
    uint32_t pages[2];
    
    // Both windows start at the beginning of the ROM.
    pages[0] = 0;
    pages[1] = 0;

    while (1) {
        // Wait for /SLTSL to fall.
        while (((pins = gpio_get_all()) & SLTSL_GPIO_MASK) == SLTSL_GPIO_MASK);

        // The line goes down at the selection, not once /RD has been decoded:
        // everything before that point is margin the Z80 gets for free.
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

        // --- READ CYCLE ---
        //
        // Both branches test /SLTSL as well as the strobe now. They did not have
        // to before, because the sample they judged was taken with /SLTSL known
        // low; the spin above can also end on /SLTSL rising, and a bare /RD test
        // would then drive the data bus into another slot's read cycle.
        if (!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK)))
        {
            uint32_t addr = (pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;

            if (addr >= 0x4000 && addr < 0xC000) {
                uint16_t page_addr = addr & 0x3FFF;
                uint16_t page = (addr >> 14) - 1;

                uint8_t data = mem[(pages[page] + page_addr) & mask];

#if defined(DATA_GPIO_SHIFT) && (DATA_GPIO_SHIFT > 0)
                gpio_put_masked(DATA_GPIO_MASK, ((uint32_t)data) << DATA_GPIO_SHIFT);
#else
                gpio_put_masked(DATA_GPIO_MASK, (uint32_t)data);
#endif
                SET_DATA_MODE_OUT;
#ifdef WITH_SHIFTERS
                gpio_put(DATADIR_PIN, 1);  // to MSX
#endif
                releaseWait();

                while (!(gpio_get_all() & RD_GPIO_MASK));

#ifdef WITH_SHIFTERS
                gpio_put(DATADIR_PIN, 0);  // to Cartridge
#endif
                SET_DATA_MODE_IN;
            } else {
                // Outside 0x4000-0xBFFF -- page 0 or page 3 -- there is
                // nothing here to answer with, so let the Z80 go without ever
                // turning the bus drivers on.
                releaseWait();
                while (!(gpio_get_all() & RD_GPIO_MASK));
            }
        }
        // --- WRITE CYCLE (mapper registers) ---
        else if (!(pins & (SLTSL_GPIO_MASK | WR_GPIO_MASK)))
        {
            // Sample again: the data is settled by the time /WR is low.
            uint32_t wr_pins = gpio_get_all();
            uint32_t addr = (wr_pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;
            
#if defined(DATA_GPIO_SHIFT) && (DATA_GPIO_SHIFT > 0)
            uint8_t data = (wr_pins & DATA_GPIO_MASK) >> DATA_GPIO_SHIFT;
#else
            uint8_t data = (wr_pins & DATA_GPIO_MASK);
#endif

            // ASCII16 registers: 0x6000-0x67FF (page 1), 0x7000-0x77FF (page 2)
            if ((addr >= 0x6000) && (addr < 0x7800) && !(addr & 0x0800))
            {
                uint16_t page = (addr >> 12) & 1; // 0 for 0x6000, 1 for 0x7000
                // Register holds a 64-bank index (1MB); the real index is
                // bounded by the ROM size when the read applies the mask.
                pages[page] = ((uint32_t)(data & 0x3F)) << 14;
            }

            releaseWait();
            while (!(gpio_get_all() & WR_GPIO_MASK));
        } else {
            releaseWait();          // /SLTSL rose with no strobe at all
        }
    }
    return true;
}
