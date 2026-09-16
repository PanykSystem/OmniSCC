/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#include "pico/multicore.h"
#include "OmniSCC.hxx"
#include <string.h>
#include "Loader.hxx"
#include "KonamiCart.hxx"
#include "KonamiSCCCart.hxx"
#include "ASCII8Cart.hxx"
#include "ASCII16Cart.hxx"
#include "DiskCart.hxx"
#include "RomPatcher.hxx"
#include "hardware/flash.h"
#include "PermStorage.hxx"
#include "hardware/clocks.h"
#include <strings.h>
#include "SCCSound.hxx"

// 4-byte aligned: the disk cartridge overlays a uint32_t CLMT scratch and a
// uint16_t sector map on the tail of this buffer, and Cortex-M0+ faults on an
// unaligned word access.
alignas(uint32_t) static uint8_t car_cache[0x2000*0x10];  // 128K

// The disk interface borrows the head of car_cache for its three RAM-resident
// buffers -- the patched 16 KB Disk ROM, the 8 KB exchange window and the
// borrow-window shadow -- instead of adding 25 KB of its own .bss. That does
// not fit: the RP2040's 224 KB of SRAM was already 99.8% full before this
// feature existed, and giving the first two their own storage alone already
// overflowed the region by 24092 bytes at link time.
//
// Borrowing is safe because car_cache is only live while a cartridge loaded by
// LoadToRAM() is current, and MountDisk() is reachable only from
// CLoader::CmdMapDisk. A CLoader never serves out of car_cache: it serves the
// embedded preloader, loader_cache, or nothing at all. Once the disk cartridge
// is current no further Mount() can run -- the main loop only calls Execute()
// on it, and it has no command that mounts anything.
//
// Phase 3's write-back image cache therefore cannot have the whole of
// car_cache: it has to start above these buffers.

// Room for the bytes CRomPatcher::Apply displaces at the borrow window. The
// window is the ROM stub's own length in assembled Z80, so this only has to be
// a comfortable bound on that; Apply is told the size and refuses rather than
// overrunning it.
#define DISK_SHADOW_SIZE 1024u

static uint8_t* const disk_rom_cache = &car_cache[0];
static uint8_t* const disk_exchange_buffer = &car_cache[DISKROM_SIZE];
static uint8_t* const disk_borrow_shadow =
    &car_cache[DISKROM_SIZE + DISK_BUF_SIZE];
static_assert(DISKROM_SIZE + DISK_BUF_SIZE + DISK_SHADOW_SIZE
                  <= sizeof(car_cache),
              "disk ROM, exchange and shadow buffers must fit inside car_cache");

// Direct disk map storage, carved from car_cache above the three disk buffers
// for the same reason they are: there is no spare .bss. It is only live while
// the disk cartridge is -- car_cache is otherwise the ROM staging buffer, and
// no ROM loads while a disk is mounted. DISK_MAP_MAX_SECTORS bounds the image
// size that gets a direct LBA -> flash map (8 MB, ample for any .dsk); a larger
// image falls back to FatFs reads. The scratch holds the cluster fragment table
// while CDiskImage builds the map.
#define DISK_MAP_MAX_SECTORS      16384u
#define DISK_CLMT_SCRATCH_DWORDS  8192u

// The second drive gets storage of its own, and a smaller allowance: drive B is
// a floppy image (1440 sectors for 720 KB), where drive A is sized for anything
// a .dsk might be. Its own scratch, not a share of drive A's, because a fallback
// mount keeps the scratch for FatFs's fast-seek table -- see CDiskImage::Mount --
// so the two cannot take turns with one buffer.
#define DISK2_MAP_MAX_SECTORS     4096u
#define DISK2_CLMT_SCRATCH_DWORDS 1024u

#define DISK_MAP_BASE (DISKROM_SIZE + DISK_BUF_SIZE + DISK_SHADOW_SIZE)
#define DISK_CLMT_BASE (DISK_MAP_BASE + DISK_MAP_MAX_SECTORS * 2u)
#define DISK2_MAP_BASE \
    (DISK_CLMT_BASE + DISK_CLMT_SCRATCH_DWORDS * sizeof(DWORD))
#define DISK2_CLMT_BASE (DISK2_MAP_BASE + DISK2_MAP_MAX_SECTORS * 2u)
#define DISK_BUFFERS_END \
    (DISK2_CLMT_BASE + DISK2_CLMT_SCRATCH_DWORDS * sizeof(DWORD))

static uint16_t* const disk_sector_map = (uint16_t*)&car_cache[DISK_MAP_BASE];
static DWORD* const disk_clmt_scratch = (DWORD*)&car_cache[DISK_CLMT_BASE];
static uint16_t* const disk2_sector_map = (uint16_t*)&car_cache[DISK2_MAP_BASE];
static DWORD* const disk2_clmt_scratch = (DWORD*)&car_cache[DISK2_CLMT_BASE];
static_assert(DISK_BUFFERS_END <= sizeof(car_cache),
              "both direct disk maps and their CLMT scratches must fit inside "
              "car_cache");


// Sector scratch for USB mass storage, lent out of car_cache. main() picks one
// mode or the other and msx_cart_main() never returns, so the two never run
// together: while MSC is serving, no cartridge exists and car_cache is idle.
// A dedicated 512-byte array instead costs .bss that this build does not have
// -- the region has overflowed twice on far less than that.
//
// The extern declaration gives the definition external linkage; a
// namespace-scope `const` is internal by default in C++.
extern "C" uint8_t* const msc_sector_scratch;
extern "C" uint8_t* const msc_sector_scratch = &car_cache[0];

// The loader/menu ROM gets its own buffer. car_cache is the staging area for the
// next Mount(), and the loader is still serving code to the Z80 while that mount
// runs, so sharing the buffer destroys the running menu when the mount fails.
// CLoader only serves 0x4000-0x5FFF, so 8 KB covers the whole window.
static uint8_t loader_cache[0x2000];    // 8K

static CSCCSound g_scc;

static bool getFirstCartFileName(char* buffer, size_t buffer_size, uint64_t* fileSize)
{
    FILINFO fno;
    DIR dir;
    FRESULT res;
    res = f_opendir(&dir, "/");   // Open root directory
    if (res == FR_OK) {
        for (;;) {
            res = f_readdir(&dir, &fno);  // Read a directory entry
            if (res != FR_OK || fno.fname[0] == 0) break;  // Error or end
            char *ext = strrchr(fno.fname, '.');
            if (ext && strcasecmp(ext, ".rom") == 0) {
                if(strlen(fno.fname) < buffer_size)
                {
                    strcpy(buffer, fno.fname);
                    *fileSize = fno.fsize;
                    f_closedir(&dir);
                    return true;
                }
            }
        }
        f_closedir(&dir);
    }
    return false;
}



COmniSCC::COmniSCC() : _diskFiles(_diskImage, _diskImage2)
{
   	// overclock to work with FLASH
#ifdef OVERCLOCK_SPEED    
    set_sys_clock_khz(OVERCLOCK_SPEED, true);
#endif    

    gpio_init_mask(ALL_GPIO_MASK);
    gpio_set_dir_in_masked(ALL_GPIO_MASK);

#ifdef WITH_SHIFTERS
    gpio_set_dir(DATADIR_PIN, GPIO_OUT);
    gpio_put(DATADIR_PIN, 0);  // to cartridge
#endif
    _current = 0;
    _prev = 0;

    g_scc.init();
    //g_scc.startupTestToneSine();
    multicore_launch_core1(CSCCSound::core1Entry);
}

COmniSCC::~COmniSCC()
{
    if(_prev)
        delete _prev;
    if(_current)
        delete _current;
}

COmniSCC::CartType COmniSCC::DetectCartType(uint8_t* data, uint64_t fileSize)
{
    if(!strncmp((const char*)&data[16], "ASCII16X", 8))
        return CT_ASCII16X;
    if(!strncmp((const char*)&data[16], "ROM_NEO8", 8))
        return CT_NE08;
    if(!strncmp((const char*)&data[16], "ROM_NE16", 8))
        return CT_NE16;
    if(!strncmp((const char*)&data[16], "PICOMSX", 8))
        return CT_Loader;

    if(fileSize < 0x10000)
    {
		if ((fileSize <= 0x4000) &&
		           (data[0] == 'A') && (data[1] == 'B')) {
			auto initAddr = uint16_t(data[2] + 256 * data[3]);
			auto textAddr = uint16_t(data[8] + 256 * data[9]);
			if ((textAddr & 0xC000) == 0x8000) {
				if ((initAddr == 0) ||
				    (((initAddr & 0xC000) == 0x8000) &&
				     (data[initAddr & (fileSize - 1)] == 0xC9))) {
					//return CT_PAGE2;
                    return CT_Plain;
				}
			}
		}
		//return CT_MIRRORED;
        return CT_Plain;
    }

    // try to guess

    uint32_t typeGuess[CT_LLast];
    for(int i = 0 ; i < CT_LLast; i++)
        typeGuess[i] = 0;

    for(int i = 0 ; i < fileSize-3; i++)
    {
        if(data[i] == 0x32)
        {
            uint16_t value = uint16_t(data[i + 1] + (data[i + 2] << 8));
            switch (value) {
            case 0x5000:
            case 0x9000:
            case 0xb000:
                typeGuess[CT_KonamiSCC]++;
                break;
            case 0x4000:
            case 0x8000:
            case 0xa000:
                typeGuess[CT_Konami]++;
                break;
            case 0x6800:
            case 0x7800:
                typeGuess[CT_ASCII8]++;
                break;
            case 0x6000:
                typeGuess[CT_Konami]++;
                typeGuess[CT_ASCII8]++;
                typeGuess[CT_ASCII16]++;
                break;
            case 0x7000:
                typeGuess[CT_KonamiSCC]++;
                typeGuess[CT_ASCII8]++;
                typeGuess[CT_ASCII16]++;
                break;
            case 0x77ff:
                typeGuess[CT_ASCII16]++;
                break;
            }
        }
    }

    int max = 0;    
    CartType type = CT_Plain;

    for(int i = 0; i < CT_LLast; i++)
    {
        if(typeGuess[i] > max)
        {
            type = (CartType) i;
            max = typeGuess[i];
        }
    }

    return type;
}

uint8_t* COmniSCC::LoadToRAM(FIL* _file)
{
    FRESULT res;
    UINT bytes_read;

    uint64_t fileSize = f_size(_file);

    if(fileSize == 0 || fileSize > sizeof(car_cache))
        return 0;

    // f_read returns FR_OK on a short read, so bytes_read has to be checked as
    // well: otherwise the tail of car_cache still holds the previously mounted
    // ROM, and both DetectCartType() and the mapper would run on stale data.
    res = f_read(_file, &car_cache[0], (UINT)fileSize, &bytes_read);
    if(res != FR_OK || bytes_read != fileSize)
        return 0;

    // Blank the rest of the buffer so a bank past the end of this ROM reads as
    // unprogrammed 0xFF instead of the previous game's code.
    if(fileSize < sizeof(car_cache))
        memset(&car_cache[fileSize], 0xFF, sizeof(car_cache) - (size_t)fileSize);

    return &car_cache[0];
}
uint8_t* COmniSCC::LoadToFlash(FIL* _file)
{
    FRESULT res;
    UINT bytes_read;

    uint64_t fileSize = f_size(_file);

    // Pad to the same power-of-two window the mapper mask exposes (see
    // CCartridge::MakeSizeMask), not just to the 4096-byte erase granularity:
    // bytes between the end of the ROM and that boundary are reachable through a
    // bank register and would otherwise still hold the previously mounted ROM.
    uint64_t alignedSize = 4096;
    while(alignedSize < fileSize)
        alignedSize <<= 1;
    
    if(alignedSize > flash_get_permanent_storge_size())
        return 0;

    uint8_t* permFlashPtr = flash_get_permanent_storge();
    
    uint64_t remainingAligned = alignedSize;
    uint64_t remainingFile = fileSize;
    uint32_t flashOffset = 0;
    
    do
    {
        uint32_t chunkSize = (remainingAligned <= sizeof(car_cache)) 
                              ? remainingAligned 
                              : sizeof(car_cache);

        // Read actual file data (might be less on last chunk)
        uint32_t toRead = (remainingFile <= chunkSize) ? remainingFile : chunkSize;
        
        res = f_read(_file, &car_cache[0], toRead, &bytes_read);
        if(res != FR_OK || bytes_read != toRead)
            return 0;

        // Pad with 0xFF if chunk is larger than what we read
        if(chunkSize > toRead)
            memset(&car_cache[toRead], 0xFF, chunkSize - toRead);

        // Write to flash - check if we need to write
        if(memcmp(permFlashPtr, &car_cache[0], chunkSize) != 0)
        {
            uint32_t ints = save_and_disable_interrupts();   
            perm_flash_range_erase(flashOffset, chunkSize);
            perm_flash_range_program(flashOffset, &car_cache[0], chunkSize);
            restore_interrupts(ints);
        }
        
        permFlashPtr += chunkSize;
        flashOffset += chunkSize;
        remainingAligned -= chunkSize;
        remainingFile = (remainingFile > toRead) ? (remainingFile - toRead) : 0;
    } while (remainingAligned > 0);

    return flash_get_permanent_storge();
}


bool COmniSCC::MountLoader()
{
    if(_current)
    {
        if(_prev)
            delete _prev;
        _prev = _current;
        _current = 0;
    }

    _current = new CLoader(*this, g_scc);

    return _current != 0;
}

bool COmniSCC::MountSCC()
{
    if(_current)
    {
        if(_prev)
            delete _prev;
        _prev = _current;
        _current = 0;
    }

    // Bare SCC: a loader with no ROM window. Power-on the chip here, since the
    // CLoader constructor deliberately does not reset it (that would wipe
    // startupTestToneSine() when the audio smoke test is enabled).
    g_scc.reset();
    _current = new CLoader(*this, g_scc, 0, false);

    return _current != 0;
}

bool COmniSCC::MountDisk(const char* imagePath, const char* romPath,
                         const char* imagePath2)
{
    // The dump to patch. The menu can name one per image through loader.ini's
    // diskrom= key, which matters for anyone who owns more than one machine:
    // the National and Yamaha dumps put their controller registers at different
    // addresses and their Disk BASIC differs, so the right dump is the one from
    // the machine the image is meant to run on.
    const char* romName = (romPath && romPath[0]) ? romPath : _diskROMName;

    FIL rom;
    if (f_open(&rom, romName, FA_READ) != FR_OK)
        return false;

    UINT got = 0;
    const FRESULT rd = f_read(&rom, disk_rom_cache, DISKROM_SIZE, &got);
    const FSIZE_t romSize = f_size(&rom);
    f_close(&rom);

    if (rd != FR_OK || got != DISKROM_SIZE || romSize != DISKROM_SIZE)
        return false;

    RomPatchInfo info;
    memset(&info, 0, sizeof(info));

    if (CRomPatcher::Validate(disk_rom_cache, DISKROM_SIZE, info) != RP_OK)
        return false;

    extern const uint8_t __diskstub_rom_start[];
    extern const uint8_t __diskstub_rom_size[];
    extern const uint8_t __diskstub_romrel_start[];
    const uint16_t stubLen = (uint16_t)(uintptr_t)__diskstub_rom_size;

    // Apply displaces stubLen vendor bytes into the shadow buffer and refuses
    // if they would not fit: the blob is a link-time size, the buffer a
    // compile-time one, and nothing else ties the two together.
    if (CRomPatcher::Apply(disk_rom_cache, disk_borrow_shadow,
                           DISK_SHADOW_SIZE, __diskstub_rom_start, stubLen,
                           __diskstub_romrel_start, info) != RP_OK)
        return false;

    // Redirect the header's STATEMENT vector at 0x4004 to the BASIC window, so
    // a CALL statement BASIC does not recognise reaches our handler there.
    //
    // Exactly once per mount, and here rather than anywhere else: PatchStatement
    // keeps the address it displaces in info.originalStatement, which is what
    // the handler chains to for the vendor's own CALL FORMAT and CALL SYSTEM. A
    // second call would capture our own entry as the "original" and the chain
    // would loop into itself. This is the only path that patches a fresh image,
    // so calling it beside Apply is what ties the two together.
    CRomPatcher::PatchStatement(disk_rom_cache, DISK_BASIC_ENTRY, info);

    _diskImage.SetDirectMap(disk_sector_map, DISK_MAP_MAX_SECTORS,
                            disk_clmt_scratch, DISK_CLMT_SCRATCH_DWORDS);
    if (!_diskImage.Mount(imagePath))
        return false;

    // Drive B, from loader.ini's disk2=. An image that will not mount is not a
    // reason to refuse the boot: the machine comes up with drive A as asked and
    // B empty, which is what the MSX sees for a drive with no disk in it anyway.
    // Refusing instead would turn a stale disk2= line into a cartridge that will
    // not start.
    _diskImage2.Unmount();
    if (imagePath2 && imagePath2[0]) {
        _diskImage2.SetDirectMap(disk2_sector_map, DISK2_MAP_MAX_SECTORS,
                                 disk2_clmt_scratch, DISK2_CLMT_SCRATCH_DWORDS);
        _diskImage2.Mount(imagePath2);
    }

    if (_current) {
        if (_prev)
            delete _prev;
        _prev = _current;
        _current = 0;
    }

    // Power-on the chip for the new cartridge, as MountSCC does: the disk
    // cartridge shares the SCC windows, and a freshly inserted interface
    // starts with a freshly reset chip.
    g_scc.reset();
    _current = new CDiskCartridge(*this, g_scc, disk_rom_cache,
                                  disk_exchange_buffer, disk_borrow_shadow,
                                  _diskImage, _diskImage2, _diskFiles, info);
    return _current != 0;
}

bool COmniSCC::MountLoaderROM(const uint8_t* data, uint64_t fileSize)
{
    if(fileSize == 0 || fileSize > sizeof(loader_cache))
        return false;

    memcpy(loader_cache, data, (size_t)fileSize);
    if(fileSize < sizeof(loader_cache))
        memset(&loader_cache[fileSize], 0xFF, sizeof(loader_cache) - (size_t)fileSize);

    _current = new CLoader(*this, g_scc, (const char*)loader_cache);

    return _current != 0;
}

bool COmniSCC::Mount(const char* fileName)
{

    FIL file;
    FRESULT res = f_open(&file, fileName, FA_READ);
    if (res == FR_OK) {
        uint64_t fileSize = f_size(&file);

        if(_current)
        {
            if(_prev)
                delete _prev;
            _prev = _current;
            _current = 0;
        }

        uint8_t* data = 0;

        if(fileSize <= sizeof(car_cache))
            data = LoadToRAM(&file);
        else
            data = LoadToFlash(&file);
            
        f_close(&file);

        if(data)
        {
            // Bound every mapper index by the real ROM size, capped to the buffer
            // the ROM actually lives in. See CCartridge::MakeSizeMask().
            const uint32_t regionLimit = (fileSize <= sizeof(car_cache))
                                            ? (uint32_t)sizeof(car_cache)
                                            : flash_get_permanent_storge_size();
            const uint32_t romMask =
                CCartridge::MakeSizeMask((uint32_t)fileSize, regionLimit);

            if(!strcasecmp(_loaderROMName, fileName))               // loader cartridge
                MountLoaderROM(data, fileSize);
            else if(strstr(fileName, "[Plain]"))                  // force plain ROM
                _current = new CCartridge(*this, data, romMask);
            else if(strstr(fileName, "[Konami]"))                  // force Konami
                _current = new CKonamiCartridge(*this, data, romMask);
            else if(strstr(fileName, "[KonamiSCC]"))               // force Konami SCC
                _current = new CKonamiSCCCartridge(*this, g_scc, data, romMask);
            else if(strstr(fileName, "[ASCII16]"))                 // force ASCII16
                _current = new CASCII16Cartridge(*this, data, romMask);
            else if(strstr(fileName, "[ASCII8]"))                  // force ASCII8
                _current = new CASCII8Cartridge(*this, data, romMask);
            else
                switch(DetectCartType(data, fileSize))
                {
                case CT_Plain:
                    _current = new CCartridge(*this, data, romMask);
                    break;
                case CT_Konami:
                    _current = new CKonamiCartridge(*this, data, romMask);
                    break;
                case CT_KonamiSCC:
                    _current = new CKonamiSCCCartridge(*this, g_scc, data, romMask);
                    break;
                case CT_ASCII8:
                    _current = new CASCII8Cartridge(*this, data, romMask);
                    break;
                case CT_ASCII16:
                    _current = new CASCII16Cartridge(*this, data, romMask);
                    break;
                case CT_Loader:
                    MountLoaderROM(data, fileSize);
                    break;

                default:
                    // Detected a mapper this firmware does not emulate
                    // (CT_ASCII16X / CT_NE08 / CT_NE16 / CT_PAGE2 / CT_MIRRORED).
                    // Leave _current at 0 so Mount() reports failure. The loader is
                    // still running from loader_cache, so it can show the error.
                    break;
                }
        }
        return _current != 0;
    }

    return false;
}

bool COmniSCC::Start()
{
    if(_prev)
    {
        delete _prev;
        _prev = 0;
    }

    if(_current)
        return _current->Execute();
    return true;
}

extern "C" void msx_cart_main()
{

   	// overclock to work with FLASH
#ifdef OVERCLOCK_SPEED    
    set_sys_clock_khz(OVERCLOCK_SPEED, true);
#endif    

    gpio_init_mask(ALL_GPIO_MASK);
    gpio_set_dir_in_masked(ALL_GPIO_MASK);

#ifdef WITH_SHIFTERS
    gpio_set_dir(DATADIR_PIN, GPIO_OUT);
    gpio_put(DATADIR_PIN, 0);  // to cartridge
#endif

    COmniSCC multiCart;
    multiCart.MountLoader();
    while(multiCart.Mounted())
    {
        multiCart.Start();
    }
}