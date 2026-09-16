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
#define MAGIC_8_BYTES "OSPC_FS1"

typedef struct {
    uint8_t header[8];
    uint16_t sectors[FAT_NUM_SECTORS];  // map FAT sectors -> flash sectors
} sector_map;

sector_map fs_map;
bool fs_map_needs_written[15];

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
void flash_read_sector(uint16_t sector, uint8_t offset, void *buffer, uint16_t size);
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

void write_fs_map()
{   
    debug_print_in_use();
    for (int i=0; i<15; i++) {
        if (fs_map_needs_written[i]) {
//          printf("Writing FS Map %d\n", i);
            flash_erase_sector(i);
            flash_write_sector(i, 0, (uint8_t*)&fs_map+(FLASH_SECTOR_SIZE*i), FLASH_SECTOR_SIZE);
            fs_map_needs_written[i] = false;
        }
    }
}

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
    for (int i=0; i<15; i++)
        used_bitmap[i] = 0xFF;    // first 15 flash sectors used by fs map

    for (int i=0; i< FAT_NUM_SECTORS; i++) {
        uint16_t mapEntry = fs_map.sectors[i];
        if (mapEntry)
            used_bitmap[getMapSector(mapEntry)] |= (1 << getMapOffset(mapEntry));
    }
    write_sector = 0;
}

int _flash_mount()
{
    for (int i=0; i<15; i++)
        fs_map_needs_written[i] = false;

    // read the first sector, with header
    flash_read_sector(0, 0, &fs_map, sizeof(fs_map));
    if (memcmp(fs_map.header, MAGIC_8_BYTES, 8) != 0) {
        return 1;
    }

    init_used_bitmap();
    debug_print_in_use();
    return 0;
}

void _flash_create()
{
    memset(&fs_map, 0, sizeof(fs_map));
    memcpy(fs_map.header, MAGIC_8_BYTES, 8);
    for (int i=0; i<15; i++)
        fs_map_needs_written[i] = true;
    write_fs_map();
    init_used_bitmap();
}

void _flash_sync()
{
    write_fs_map();
}

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


void flash_read_sector(uint16_t sector, uint8_t offset, void *buffer, uint16_t size)
{
//  printf("[FS] READ: %d, %d (%d)\n", sector, offset, size);
    uint32_t off = HW_FLASH_STORAGE_BASE + (sector * FLASH_SECTOR_SIZE) + (offset * 512);
    uint32_t addr = XIP_BASE + off;
    memcpy(buffer, (unsigned char *)addr, size);
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