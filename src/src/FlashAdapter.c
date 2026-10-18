/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

 #include <stdio.h>
#include <string.h>
#include "FlashAdapter.h"

#define HW_FLASH_STORAGE_BASE  FS_BASE
#define MAGIC_8_BYTES "OSPC_FS2"
#define FORMATTING_MAGIC "OSPC_FM2"
// Legacy on-flash allocator reserves sectors 0..14; preserve existing maps.
#define FS_MAP_RESERVED_SECTORS 15

typedef struct {
    uint8_t header[8];
    uint16_t sectors[FAT_NUM_SECTORS];  // map FAT sectors -> flash sectors
} sector_map;
_Static_assert(sizeof(sector_map) <= FS_MAP_RESERVED_SECTORS * FLASH_SECTOR_SIZE, "flash map exceeds metadata reserve");

// Outside the FTL and the 512 KiB executable: ordinary map rewrites must
// never erase the identity used to decide whether formatting is permitted.
#define LAYOUT_ID_OFFSET (FLASH_FIRMWARE_RESERVED_BYTES - FLASH_SECTOR_SIZE)
#define LAYOUT_ID_MAGIC "OSPC_ID2"
#define LAYOUT_FORMATTING 0xFEu
#define LAYOUT_COMMITTED 0xFCu
static uint8_t layout_identity(void)
{
    const uint8_t* id = (const uint8_t*)(uintptr_t)(XIP_BASE + LAYOUT_ID_OFFSET);
    return !memcmp(id, LAYOUT_ID_MAGIC, 8) ? id[8] : 0xFF;
}
static bool write_layout_identity(uint8_t state)
{
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, LAYOUT_ID_MAGIC, 8);
    page[8] = state;
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_program(LAYOUT_ID_OFFSET, page, sizeof(page));
    restore_interrupts(ints);
    return !memcmp((const void*)(uintptr_t)(XIP_BASE + LAYOUT_ID_OFFSET), page, sizeof(page));
}

FlashLayoutState _flash_layout_state(void)
{
    const uint8_t identity = layout_identity();
    if (identity == LAYOUT_COMMITTED) return FLASH_LAYOUT_CURRENT;
    if (identity == LAYOUT_FORMATTING) return FLASH_LAYOUT_FORMATTING;
    const uint8_t* current = (const uint8_t*)(uintptr_t)(XIP_BASE + FS_BASE);
    if (!memcmp(current, MAGIC_8_BYTES, 8)) return FLASH_LAYOUT_CURRENT;
    if (!memcmp(current, FORMATTING_MAGIC, 8)) return FLASH_LAYOUT_FORMATTING;
    const uint8_t* old = (const uint8_t*)(uintptr_t)(XIP_BASE + 2097152u);
    if (!memcmp(old, "OSPC_FS1", 8)) {
        const uint16_t* entries = (const uint16_t*)(old + 8);
        const uint32_t oldSectors = (PICO_FLASH_SIZE_BYTES - 2097152u) / 4096;
        bool valid = true;
        for (uint32_t i = 0; i < (oldSectors-16)*8; ++i) {
            const uint32_t sector = entries[i] >> 3;
            if (entries[i] && (sector < 15 || sector >= oldSectors)) { valid = false; break; }
        }
        if (valid) return FLASH_LAYOUT_LEGACY;
    }
    return _flash_is_blank() ? FLASH_LAYOUT_BLANK : FLASH_LAYOUT_UNKNOWN;
}

sector_map fs_map;
bool fs_map_needs_written[FS_MAP_RESERVED_SECTORS];

uint8_t used_bitmap[NUM_FLASH_SECTORS];    // we will use 256 flash sectors for 2048 fat sectors

uint16_t write_sector = 0;   // which flash sector we are writing to
uint8_t write_sector_bitmap = 0;   // 1 for each free 512 byte page on the sector

// each sector entry in the sector map is:
//  13 bits of sector (indexing 8192 4k flash sectors)
//   3 bits of offset (0->7 512 byte FAT sectors in each 4k flash sector)
uint16_t getMapSector(uint16_t mapEntry) { return (mapEntry & 0xFFF8) >> 3; }
uint8_t getMapOffset(uint16_t mapEntry) { return mapEntry & 0x7; }
uint16_t makeMapEntry(uint16_t sector, uint8_t offset) { return (sector << 3) | offset; };

// forward declns
void __attribute__((noinline, noclone)) __not_in_flash_func(flash_read_sector)(uint16_t sector, uint8_t offset, void *buffer, uint16_t size);
void flash_erase_sector(uint16_t sector);
void flash_write_sector(uint16_t sector, uint8_t offset, const void *buffer, uint16_t size);
void flash_erase_with_copy_sector(uint16_t sector, uint8_t preserve_bitmap);

void debug_print_in_use() {
    return;
    // just shows first 1meg
    printf("IN USE-----------------------------------\n");
    for (int i=0; i<16; i++) {
        printf("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            used_bitmap[i*16+0], used_bitmap[i*16+1], used_bitmap[i*16+2], used_bitmap[i*16+3],
            used_bitmap[i*16+4], used_bitmap[i*16+5], used_bitmap[i*16+6], used_bitmap[i*16+7],
            used_bitmap[i*16+8], used_bitmap[i*16+9], used_bitmap[i*16+10], used_bitmap[i*16+11],
            used_bitmap[i*16+12], used_bitmap[i*16+13], used_bitmap[i*16+14], used_bitmap[i*16+15]);
    }
    printf("END--------------------------------------\n");
}

bool write_fs_map_checked(void)
{
    uint8_t page[FLASH_PAGE_SIZE];
    const unsigned blocks = (sizeof(fs_map) + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    for (unsigned i = 0; i < blocks; ++i) {
        if (!fs_map_needs_written[i]) continue;
        flash_erase_sector(i);
        for (unsigned off = 0; off < FLASH_SECTOR_SIZE; off += FLASH_PAGE_SIZE) {
            const unsigned position = i * FLASH_SECTOR_SIZE + off;
            unsigned n = position < sizeof(fs_map) ? sizeof(fs_map) - position : 0;
            if (n > sizeof(page)) n = sizeof(page);
            memset(page, 0xFF, sizeof(page));
            if (n) memcpy(page, (uint8_t*)&fs_map + position, n);
            uint32_t ints = save_and_disable_interrupts();
            flash_range_program(HW_FLASH_STORAGE_BASE + i * FLASH_SECTOR_SIZE + off, page, sizeof(page));
            restore_interrupts(ints);
        }
        // Read back only the valid map bytes. Leave failed blocks dirty for retry.
        for (unsigned off = 0; off < FLASH_SECTOR_SIZE; off += FLASH_PAGE_SIZE) {
            const unsigned position = i * FLASH_SECTOR_SIZE + off;
            if (position >= sizeof(fs_map)) break;
            unsigned n = sizeof(fs_map) - position;
            if (n > sizeof(page)) n = sizeof(page);
            const uint8_t* persisted = (const uint8_t*)(uintptr_t)(XIP_BASE + HW_FLASH_STORAGE_BASE + i * FLASH_SECTOR_SIZE + off);
            if (memcmp(persisted, (uint8_t*)&fs_map + position, n)) return false;
        }
        fs_map_needs_written[i] = false;
    }
    return true;
}
void write_fs_map(void) { (void)write_fs_map_checked(); }

uint16_t getNextWriteSector()
{
    static uint16_t search_start_pos = 0;
    int i;
    if (write_sector == 0 || write_sector_bitmap == 0)
    {   // first try to find a completely free sector
        for (i=0; i<NUM_FLASH_SECTORS; i++) {
            int pos = (i + search_start_pos) % NUM_FLASH_SECTORS;
            if (used_bitmap[pos] == 0)
                break;
        }
        if (i < NUM_FLASH_SECTORS) {
           write_sector = (i + search_start_pos) % NUM_FLASH_SECTORS;
           write_sector_bitmap = 0xFF;
           flash_erase_sector(write_sector);
        }
        else
        {   // no completely free sector: take the first one with a free page
            for (i=0; i<NUM_FLASH_SECTORS; i++) {
                if (used_bitmap[(i + search_start_pos) % NUM_FLASH_SECTORS] != 0xFF)
                    break;
            }
            if (i >= NUM_FLASH_SECTORS)
                return 0;   // genuinely full: every page of every sector is in
                            // use. Return the "unallocated" sentinel (0 is a
                            // reserved sector, never a real entry) instead of
                            // falling through to fabricate offset 8, which
                            // makeMapEntry would fold into the sector field and
                            // scribble over the next flash sector.
            write_sector = (i + search_start_pos) % NUM_FLASH_SECTORS;
            write_sector_bitmap = ~used_bitmap[write_sector];
            flash_erase_with_copy_sector(write_sector, used_bitmap[write_sector]);
        }
        search_start_pos = (i + search_start_pos) % NUM_FLASH_SECTORS;
    }
    // At least one 512-byte page is free on write_sector. Find it; guard the
    // search so a bitmap that is unexpectedly empty fails cleanly rather than
    // returning offset 8.
    for (i=0; i<8; i++) {
        if (write_sector_bitmap & (1 << i))
            break;
    }
    if (i >= 8)
        return 0;
    // mark the offset used
    write_sector_bitmap &= ~(1 << i);
    return makeMapEntry(write_sector, i);
}

void init_used_bitmap() {
    memset(used_bitmap, 0, NUM_FLASH_SECTORS);
    for (int i=0; i<FS_MAP_RESERVED_SECTORS; i++)
        used_bitmap[i] = 0xFF;    // first 15 flash sectors used by fs map

    for (int i=0; i< FAT_NUM_SECTORS; i++) {
        uint16_t mapEntry = fs_map.sectors[i];
        if (mapEntry)
            used_bitmap[getMapSector(mapEntry)] |= (1 << getMapOffset(mapEntry));
    }
    write_sector = 0;
}

bool _flash_is_blank(void)
{
    const volatile uint8_t* storage = (const volatile uint8_t*)(uintptr_t)(XIP_BASE + HW_FLASH_STORAGE_BASE);
    for (uint32_t i = 0; i < FS_SIZE; ++i) if (storage[i] != 0xFF) return false;
    return true;
}

int _flash_mount()
{
    if (layout_identity() == LAYOUT_FORMATTING) return 1;
    for (int i=0; i<FS_MAP_RESERVED_SECTORS; i++)
        fs_map_needs_written[i] = false;

    // Validate persisted entries before replacing a live mapping or indexing
    // used_bitmap. An interrupted erase may leave valid magic but FF entries.
    const sector_map* persisted = (const sector_map*)(uintptr_t)(XIP_BASE + HW_FLASH_STORAGE_BASE);
    if (memcmp(persisted->header, MAGIC_8_BYTES, 8) != 0) return 1;
    for (uint32_t i = 0; i < FAT_NUM_SECTORS; ++i) {
        const uint16_t entry = persisted->sectors[i];
        if (entry && (getMapSector(entry) < FS_MAP_RESERVED_SECTORS ||
                      getMapSector(entry) >= NUM_FLASH_SECTORS)) return 1;
    }
    flash_read_sector(0, 0, &fs_map, sizeof(fs_map));

    init_used_bitmap();
    debug_print_in_use();
    return 0;
}

bool _flash_create()
{
    if (layout_identity() != LAYOUT_FORMATTING) {
        const uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(LAYOUT_ID_OFFSET, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);
        if (!write_layout_identity(LAYOUT_FORMATTING)) return false;
    }
    memset(&fs_map, 0, sizeof(fs_map));
    memcpy(fs_map.header, FORMATTING_MAGIC, 8);
    for (int i=0; i<FS_MAP_RESERVED_SECTORS; i++)
        fs_map_needs_written[i] = true;
    if (!write_fs_map_checked()) return false;
    init_used_bitmap();
    return true;
}

bool _flash_sync_checked(void) { return write_fs_map_checked(); }
bool _flash_finish_format(void)
{
    if (!_flash_sync_checked()) return false;
    memcpy(fs_map.header, MAGIC_8_BYTES, 8);
    fs_map_needs_written[0] = true;
    if (!_flash_sync_checked()) return false;
    // FE -> FC clears one bit. No erase window can lose the durable identity.
    return write_layout_identity(LAYOUT_COMMITTED);
}
void _flash_sync() { (void)_flash_sync_checked(); }

void _flash_fs_read(uint16_t fat_sector, void *buffer)
{
    int mapEntry = fs_map.sectors[fat_sector];
    if (mapEntry)
        flash_read_sector(getMapSector(mapEntry), getMapOffset(mapEntry), buffer, 512);
    else
        memset(buffer, 0, 512);
    return;
}

bool _flash_fs_write(uint16_t fat_sector, const void *buffer)
{
    if (fat_sector >= FAT_NUM_SECTORS) return false;

    uint16_t mapEntry = fs_map.sectors[fat_sector];
    if (mapEntry)
    {   // Free the previous page first. That guarantees a rewrite always finds
        // space -- the page just freed -- so getNextWriteSector returns 0 only
        // for a genuinely full disk on a brand-new sector, where nothing was
        // freed and there is nothing to roll back. A reclaim during allocation
        // preserves the other pages of the sector, never this one.
        used_bitmap[getMapSector(mapEntry)] &= ~(1 << getMapOffset(mapEntry));
    }

    mapEntry = getNextWriteSector();
    if (mapEntry == 0)
        return false;               // disk full (new sector; nothing was freed)

    fs_map.sectors[fat_sector] = mapEntry;
    if (fat_sector < 2044)
        fs_map_needs_written[0] = true;
    else
        fs_map_needs_written[1+((fat_sector-2044)/2048)] = true;

    used_bitmap[getMapSector(mapEntry)] |= (1 << getMapOffset(mapEntry));

    flash_write_sector(getMapSector(mapEntry), getMapOffset(mapEntry), buffer, 512);
    return true;
}

bool _flash_fs_verify(uint16_t fat_sector, const void *buffer)
{
    uint8_t read_buf[512];
    _flash_fs_read(fat_sector, read_buf);
    if (memcmp(buffer, read_buf, 512) == 0) return true;
    return false;
}


bool __not_in_flash_func(flash_get_xip_page)(uint32_t logicalSector, uint32_t* address)
{
    if (!address || logicalSector >= FAT_NUM_SECTORS) return false;
    const uint16_t entry = fs_map.sectors[logicalSector];
    const uint32_t sector = entry >> 3;
    if (sector < FS_MAP_RESERVED_SECTORS || sector >= NUM_FLASH_SECTORS) return false;
    *address = XIP_BASE + HW_FLASH_STORAGE_BASE + (uint32_t)entry * 512u;
    return true;
}

void __attribute__((noinline, noclone)) __not_in_flash_func(flash_read_sector)(uint16_t sector, uint8_t offset, void *buffer, uint16_t size)
{
//  printf("[FS] READ: %d, %d (%d)\n", sector, offset, size);
    uint32_t off = HW_FLASH_STORAGE_BASE + (sector * FLASH_SECTOR_SIZE) + (offset * 512);
    uint32_t addr = XIP_BASE + off;
    const volatile uint8_t* source = (const volatile uint8_t*)(uintptr_t)addr;
    uint8_t* dest = buffer;
    for (uint32_t i = 0; i < size; ++i) dest[i] = source[i];
}

void flash_erase_sector(uint16_t sector)
{
//  printf("[FS] ERASE: %d\n", sector);
    uint32_t fs_start = HW_FLASH_STORAGE_BASE;
    uint32_t offset = fs_start + (sector * FLASH_SECTOR_SIZE);
    uint32_t ints = save_and_disable_interrupts();   
    flash_range_erase(offset, FLASH_SECTOR_SIZE);  
    restore_interrupts(ints);
}

void flash_write_sector(uint16_t sector, uint8_t offset, const void *buffer, uint16_t size)
{
    uint32_t fs_start = HW_FLASH_STORAGE_BASE;
    uint32_t addr = fs_start + (sector * FLASH_SECTOR_SIZE) + (offset * 512);

    if (addr % FLASH_PAGE_SIZE) return;
    if (size % FLASH_PAGE_SIZE) return;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(addr, (const uint8_t *)buffer, size);
    restore_interrupts(ints);
}

void flash_erase_with_copy_sector(uint16_t sector, uint8_t preserve_bitmap)
{
//  printf("[FS] ERASE with COPY: %d\n", sector);
    uint8_t buf[FLASH_SECTOR_SIZE];
    flash_read_sector(sector, 0, buf, FLASH_SECTOR_SIZE);
    flash_erase_sector(sector);
    for (int i=0; i<8; i++) {
        if (preserve_bitmap & (1 << i))
           flash_write_sector(sector, i, buf + (i * 512), 512);
    }
}
