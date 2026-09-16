/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

 #include "Cartridge.hxx"

CCartridge::CCartridge(COmniSCC& multiCart, uint8_t* data, uint32_t sizeMask)
    :_data(data), _mask(sizeMask), _multiCart(multiCart)
{
}

uint32_t CCartridge::MakeSizeMask(uint32_t size, uint32_t limit)
{
    if(size == 0 || limit == 0)
        return 0;

    uint32_t pow2 = 1;
    while(pow2 < size && pow2 < limit)
        pow2 <<= 1;

    return pow2 - 1;
}

CCartridge::~CCartridge()
{
}

bool __not_in_flash_func(CCartridge::Execute())
{
    const uint8_t* mem = _data;
    const uint32_t mask = _mask;
    uint32_t pins, addr;

    while (1) {
		while (((pins = gpio_get_all()) & (SLTSL_GPIO_MASK)) == SLTSL_GPIO_MASK);    // wait for SLTSL access

        if(!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK)))           // read memory of cartridge
        {
            addr = (pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;
            // Unsigned wrap for addr < 0x4000; the mask folds it back into the ROM.
            uint8_t data = mem[(addr - 0x4000) & mask];
            SET_DATA_MODE_OUT;
            gpio_put_masked(DATA_GPIO_MASK, (uint32_t)(data));
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 1);  // to msx 
#endif
            while (!(gpio_get_all() & RD_GPIO_MASK));
            //for(volatile int cnt = 0; cnt < 3; cnt++);            
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 0);  // to cartridge  
#endif
            SET_DATA_MODE_IN;
        }
   }
}
