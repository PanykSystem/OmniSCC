/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#include "KonamiCart.hxx"


CKonamiCartridge::CKonamiCartridge(COmniSCC& multiCart, uint8_t* data, uint32_t sizeMask): CCartridge(multiCart, data, sizeMask)
{

}

CKonamiCartridge::~CKonamiCartridge() 
{

}

//#pragma GCC push_options
//#pragma GCC optimize("O3")

bool __not_in_flash_func(CKonamiCartridge::Execute())
{
    const uint8_t* mem = _data;
    const uint32_t mask = _mask;
    uint32_t pins;
    // Byte offsets, not pointers: the mask is applied at the read, so a bogus
    // bank register can never index outside the ROM.
    uint32_t pages[4];
    pages[0] = 0x0000;
    pages[1] = 0x2000;
    pages[2] = 0x4000;
    pages[3] = 0x6000;

    while (1) {
        while (((pins = gpio_get_all()) & (SLTSL_GPIO_MASK)) == SLTSL_GPIO_MASK);    // wait for X1 access

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

        if(!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK)))           // read memory of cartridge
        {
            uint32_t addr = ((pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT);
            uint16_t page_addr = addr & 0x1FFF;
            uint16_t page = addr >> 13;
            page &= 0x5;
            if(page > 3)
                page -= 2;

            uint8_t data = mem[(pages[page] + page_addr) & mask];

            gpio_put_masked(DATA_GPIO_MASK, (uint32_t)(data));
            SET_DATA_MODE_OUT;
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 1);  // to msx 
#endif
            releaseWait();
            while (!(gpio_get_all() & RD_GPIO_MASK));
            //for(volatile int cnt = 0; cnt < 5; cnt++);            
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 0);  // to cartridge  
#endif
            SET_DATA_MODE_IN;

        } else if (!(pins & (SLTSL_GPIO_MASK | WR_GPIO_MASK)))   // write page address
        {
            uint8_t data = pins & DATA_GPIO_MASK;
            uint32_t addr = (pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;
            uint16_t page = ((addr >> 13) - 2) & 0x3;
            
            // 0x6000 (0x6000 - 0x7fff)
            // 0x8000 (0x8000 - 0x9fff)
            // 0xA000 (0xA000 - 0xBfff)
            //
            // A write outside the register range still has to let the Z80 go:
            // the line is asserted from the selection now, so an early exit
            // that skipped the release would stall the machine.
            if ((addr >= 0x6000) && (addr < 0xC000)) {
                pages[page] = ((uint32_t)(data & 0x1f)) << 13;
            }

            releaseWait();
            while (!(gpio_get_all() & WR_GPIO_MASK));
        } else {
            releaseWait();          // /SLTSL rose with no strobe at all
        }
    }
    return true;
}
//#pragma GCC pop_options