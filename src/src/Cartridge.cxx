/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#include "Cartridge.hxx"

CCartridge::CCartridge(COmniSCC& multiCart, uint8_t* data, uint32_t sizeMask)
    :_storage{RomStorageKind::Ram,sizeMask+1,sizeMask,0,data,nullptr}, _data(data), _mask(sizeMask), _multiCart(multiCart)
{
    _plain = PlainRomLayout::Make(_storage.size, PlainRomMode::Mirrored,
        [this](uint32_t offset) {
            return _storage.kind == RomStorageKind::Ram
                ? _storage.Read<RomStorageKind::Ram>(offset) : _storage.Read<RomStorageKind::Mapped>(offset);
        });
}

CCartridge::CCartridge(COmniSCC& multiCart, const RomStorage& storage, PlainRomMode mode)
    : _storage(storage), _data(const_cast<uint8_t*>(storage.ram)), _mask(storage.mask), _multiCart(multiCart)
{
    _plain = PlainRomLayout::Make(storage.size, mode,
        [&storage](uint32_t offset) {
            return storage.kind == RomStorageKind::Ram
                ? storage.Read<RomStorageKind::Ram>(offset) : storage.Read<RomStorageKind::Mapped>(offset);
        });
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
    return _storage.kind == RomStorageKind::Ram ? ExecuteStorage<RomStorageKind::Ram>() : ExecuteStorage<RomStorageKind::Mapped>();
}

template<RomStorageKind Kind>
bool __attribute__((noinline, section(".time_critical.Cartridge_storage"))) CCartridge::ExecuteStorage()
{


    uint32_t pins, addr;

    while (1) {
		while (((pins = gpio_get_all()) & (SLTSL_GPIO_MASK)) == SLTSL_GPIO_MASK);    // wait for SLTSL access
        // Placement and non-power-of-two mirroring need more decode work than
        // the old fixed subtraction. Protect both RAM and mapped reads with
        // WAIT, asserted before the strobe so no intermediate pulse is exposed.
        forceWait();
        while ((pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK | WR_GPIO_MASK)) == (RD_GPIO_MASK | WR_GPIO_MASK)) pins = gpio_get_all();

        if(!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK)))           // read memory of cartridge
        {
            addr = (pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;
            uint32_t offset;
            if (!_plain.Offset(uint16_t(addr), _storage.mask, offset)) {
                releaseWait();
                while (!(gpio_get_all() & RD_GPIO_MASK)) {}
                continue; // Unmapped pages leave the bus tri-stated for the full cycle.
            }
            uint8_t data = _storage.Read<Kind>(offset);
            SET_DATA_MODE_OUT;
            gpio_put_masked(DATA_GPIO_MASK, (uint32_t)(data));
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 1);  // to msx 
#endif
            releaseWait();
            while (!(gpio_get_all() & RD_GPIO_MASK));
            //for(volatile int cnt = 0; cnt < 3; cnt++);            
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 0);  // to cartridge  
#endif
            SET_DATA_MODE_IN;
        } else if (!(pins & (SLTSL_GPIO_MASK | WR_GPIO_MASK))) {
            releaseWait();
            while (!(gpio_get_all() & WR_GPIO_MASK)) {}
        } else { releaseWait(); }
   }
}

bool __not_in_flash_func(CGeneric8kBCartridge::Execute())
{
    return _storage.kind == RomStorageKind::Ram
        ? ExecuteStorage<RomStorageKind::Ram>() : ExecuteStorage<RomStorageKind::Mapped>();
}

template<RomStorageKind Kind>
bool __attribute__((noinline, section(".time_critical.Generic8kB_storage"))) CGeneric8kBCartridge::ExecuteStorage()
{
    // Individual stores prevent a flash-backed initializer in the RAM service.
    uint32_t pages[4];
    pages[0] = 0;
    pages[1] = 0x2000;
    pages[2] = 0x4000;
    pages[3] = 0x6000;
    while (true) {
        uint32_t pins;
        while (((pins = gpio_get_all()) & SLTSL_GPIO_MASK) == SLTSL_GPIO_MASK) {}
        forceWait();
        while ((pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK | WR_GPIO_MASK)) == (RD_GPIO_MASK | WR_GPIO_MASK)) {
            pins = gpio_get_all();
        }
        const uint32_t addr = (pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;
        if (!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK))) {
            if (addr >= 0x4000 && addr < 0xc000) {
                const uint8_t data = _storage.Read<Kind>(pages[(addr >> 13) - 2] + (addr & 0x1fff));
                SET_DATA_MODE_OUT;
                gpio_put_masked(DATA_GPIO_MASK, uint32_t(data));
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
            if (addr >= 0x4000 && addr < 0xc000) {
                const uint8_t data = uint8_t(pins & DATA_GPIO_MASK);
                pages[(addr >> 13) - 2] = uint32_t(data) << 13;
            }
            releaseWait();
            while (!(gpio_get_all() & WR_GPIO_MASK)) {}
        } else releaseWait();
    }
}
