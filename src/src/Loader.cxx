#include "Loader.hxx"
#include "OmniSCC.hxx"
#include <string.h>
#include <stdio.h> // For snprintf

// Inline assembly macro to embed a binary file into a named linker section.
// The resulting symbol points to the raw bytes and _sizeof_<sym> gives the size.
#define IMPORT_BIN(sect, file, sym) asm (\
    ".section " #sect "\n"                  /* Place into named section */\
    ".balign 4\n"                           /* Word-align the payload */\
    ".global " #sym "\n"                    /* Export the object address */\
    #sym ":\n"                              /* Define the object label */\
    ".incbin \"" file "\"\n"                /* Include the binary payload */\
    ".global _sizeof_" #sym "\n"            /* Export the object size */\
    ".set _sizeof_" #sym ", . - " #sym "\n" /* Calculate size in bytes */\
    ".balign 4\n"                           /* Word-align after payload */\
    ".section \".text\"\n")                 /* Return to .text section */

// Embed the 8 KB hardware preloader ROM that the Pico serves to the MSX on reset.
// The Z80 copies this stub to RAM and uses it to request loader.rom.
#ifndef OMNISCC_GENERATED_DIR
#define OMNISCC_GENERATED_DIR "../generated"
#endif
IMPORT_BIN(".rodata.__car_data", OMNISCC_GENERATED_DIR "/preloader.rom", __car_start);

extern const char __car_start[];

// Static 8 KB buffer shared with the MSX through the 0x6000-0x7FFE window.
// It is static because the Z80 may still read it after a command returns.
uint8_t CLoader::exchange_buffer[LOADER_BUFFER_SIZE];

CLoader::CLoader(COmniSCC& multiCart, CSCCSound& scc, const char* _data, bool hasRom) : CCartridge(multiCart, 0, 0), _scc(scc)
{
    return_buffer = false;
    last_cmd_result = 0;
    
    memset(exchange_buffer, 0, LOADER_BUFFER_SIZE);

    if(!hasRom)
        // Bare SCC cartridge: no ROM window, so reads there stay at 0xFF.
        _car_data = 0;
    else if(!_data)
        // No external image supplied: serve the embedded preloader. The Z80
        // will later ask us to map loader.rom.
        _car_data = &__car_start[0];
    else
        _car_data = _data;
}

CLoader::~CLoader() 
{
    // exchange_buffer is static; do not free it.
}

// Set to true by CmdMapFile / CmdMapSCC / CmdMapDisk to tell Execute() to
// return. The main loop keeps running until the Z80 has read the final
// status (0xFF).
static volatile bool _exitFlag = false;

/**
 * Main command-polling loop.
 *
 * This function must live in SRAM (__not_in_flash_func) because flash
 * wait-states would violate the MSX Z80 bus timing.
 *
 * Protocol:
 *   - Z80 writes a command byte to 0x7FFF.
 *   - The Pico completes the command and stores a result in last_cmd_result.
 *   - Z80 reads 0x7FFF to fetch the result. 0xFF means "busy / not ready".
 *   - Commands that return data set return_buffer = true, allowing the Z80 to
 *     read payload from 0x6000-0x7FFE. CMD_CLOSE_BUFFER (0x00) clears it.
 *
 * The loop terminates once a mapper-switch command has been processed AND the
 * Z80 has read the final status byte.
 */
bool __not_in_flash_func(CLoader::Execute())
{
    // Core-0 interrupts stay off for the whole bus loop. An interrupt taken
    // between two polls costs Z80 bus cycles, and a missed cycle cannot be
    // recovered. They are restored around command dispatch, which is the only
    // part of this loop that blocks for any length of time.
    uint32_t const irq_state = save_and_disable_interrupts();

    uint32_t pins;
    const char *car_data = _car_data;
    _exitFlag = false;
    last_cmd_result = 0xff;
    bool sccEnabled = false;

    // Whether the Z80 has read the standing result. It used to be inferred from
    // last_cmd_result going back to 0xFF on the read itself, which is why the
    // read consumed it -- and that made one lost cycle unrecoverable. Tracking
    // it separately lets the answer repeat: a duplicated read just sets this
    // flag twice, and a lost one leaves it clear, which keeps the loop serving
    // until the Z80 really has the status.
    bool resultTaken = false;

    while (!_exitFlag || !resultTaken) {
        // Spin until the MSX selects this cartridge slot (/SLTSL low).
        while (((pins = gpio_get_all()) & (SLTSL_GPIO_MASK)) == SLTSL_GPIO_MASK);

        // Stretch the cycle before /RD or /WR is even decoded. Asserting /WAIT
        // on reads alone is not enough: an SCC driver writes its mode-select
        // register once at init, and losing that single write leaves the SCC
        // invisible for the rest of the session.
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

        // ====================================================================
        // READ PATH: MSX reads data from the cartridge window.
        // ====================================================================
        if (!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK)))
        {
            uint32_t addr = ((pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT);
            uint8_t data = 0xFF;

            // 1. Command/status register at 0x7FFF. The answer repeats until the
            //    next command write clears it; reading it has no side effect.
            //
            //    It used to consume the result -- read once, back to 0xFF -- and
            //    that made a single lost bus cycle fatal rather than survivable.
            //    This loop can answer the same read twice (it looks for /SLTSL
            //    low again without waiting for it to rise, so a cycle still in
            //    progress passes the test), and after a released-/WAIT handler it
            //    can answer a cycle the Z80 has already finished sampling. Either
            //    way the status went to nobody and the next poll read got 0xFF:
            //    the Z80 then spins on "still busy" for ever, with no way back,
            //    because the result no longer exists. Repeating the answer costs
            //    nothing and turns that into one more poll iteration.
            //    CDiskCartridge has always answered this way.
            if (addr == CMD_REG_ADDR) {
                 data = last_cmd_result;
                 if (data != 0xFF) {
                     resultTaken = true;
                 }
            }
            // 1b. The other half of the reply: while a result stands this
            //     address answers a fixed sentinel, so the Z80 can tell the
            //     firmware driving the bus from a bus nobody drives. Ahead of
            //     the buffer branch on purpose -- the reply repeats for as long
            //     as the result stands, which is exactly while the Z80 is
            //     reading a payload out, so this byte is shadowed throughout.
            //     See LOADER_READY_SENTINEL and LOADER_PAYLOAD_MAX.
            else if (addr == STATUS_REG_ADDR && last_cmd_result != 0xFF) {
                data = LOADER_READY_SENTINEL;
            }
            // 2. Exchange buffer, but only when a command has explicitly opened it.
            //    Keeping it closed by default prevents the MSX BIOS from seeing
            //    stale data during interrupt handling.
            else if (return_buffer && (addr >= BUFFER_START_ADDR) && (addr <= BUFFER_END_ADDR)) {
                data = exchange_buffer[addr - BUFFER_START_ADDR];
            }
            // 3. Normal ROM read from the preloader/loader image at 0x4000-0x5FFF.
            else {
                // SCC register reads have priority over ROM reads.
                if (sccEnabled && (addr >= 0x9800) && (addr < 0xA000)) {
                    data = _scc.readRegister((uint8_t)addr);
                }
                // car_data is aligned to 0x4000, so subtract the MSX base
                // address. It is null for the bare SCC cartridge, which has no
                // ROM: those reads keep the 0xFF default.
                else if (car_data && addr >= 0x4000 && addr < 0x6000) {
                    data = car_data[addr - 0x4000];
                }
            }

            // Drive the byte onto the data bus. The DATA_GPIO_MASK already aligns
            // the value with GPIO0-GPIO7, so no extra shifting is needed.
            gpio_put_masked(DATA_GPIO_MASK, ((uint32_t)data));
            SET_DATA_MODE_OUT;
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 1);  // Shifter: drive bus towards MSX
#endif
            releaseWait();

            // Keep driving the bus until the Z80 releases /RD.
            while (!(gpio_get_all() & RD_GPIO_MASK));

#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 0);  // Shifter: let MSX drive the bus
#endif
            SET_DATA_MODE_IN;
        }

        // ====================================================================
        // WRITE PATH: MSX writes data to the cartridge window.
        // ====================================================================
        else if (!(pins & (SLTSL_GPIO_MASK | WR_GPIO_MASK)))
        {
            // Data is already on GPIO0-GPIO7, so masking gives the byte directly.
            uint8_t data = pins & DATA_GPIO_MASK;
            uint32_t addr = (pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;

            // 1. COMMAND REGISTER: a write to 0x7FFF starts a new command.
            if (addr == CMD_REG_ADDR) {
                // Mark the command busy BEFORE the Z80 is let go. The reply
                // repeats until the next command write, so the previous
                // command's status is still standing at this point: release
                // /WAIT first and the Z80's first poll read can see that stale
                // status and take the new command for finished. The three nops
                // the Z80 side puts after the command write used to be what
                // covered this window; ordering it correctly is what actually
                // closes it.
                //
                // Close the exchange buffer for the new command too; individual
                // handlers reopen it when they need to return data.
                return_buffer = false;
                last_cmd_result = 0xFF; // busy until the handler finishes
                resultTaken = false;    // ... and the new result is unread

                // Handlers block for milliseconds on file I/O, so drop /WAIT and
                // let interrupts back in before running one. The Z80 polls for
                // the result rather than being stalled -- from MSX RAM, because
                // nothing is served while the handler runs.
                releaseWait();
                restore_interrupts(irq_state);

                switch(data) {
                    // --- System commands ---
                    case 0x00: last_cmd_result = 0; break; // CMD_CLOSE_BUFFER
                    case 0x01: CmdMapFile(); break;        // CMD_MAP_FILE
                    case 0x02: CmdMapSCC(); break;         // CMD_MAP_SCC
                    case 0x03: CmdMapDisk(); break;        // CMD_MAP_DISK

                    // --- Filesystem commands ---
                    case 0x10: CmdFileOpenRead(); break;
                    case 0x11: CmdFileOpenWrite(); break;
                    case 0x12: CmdFileReadBlock(); break;
                    case 0x13: CmdFileWriteBlock(); break;
                    case 0x14: CmdFileClose(); break;

                    // --- Directory commands ---
                    case 0x20: CmdDirOpen(); break;
                    case 0x21: CmdDirReadNext(); break;
                    case 0x22: CmdDirSeek(); break;
                    case 0x23: CmdDirClose(); break;

                    default:
                        last_cmd_result = 0xFE; // Unknown command
                        break;
                }

                // Back to tight bus polling -- but not before the bus is
                // between cycles. The handler ran with the Z80 polling 0x7FFF
                // unattended, so the loop top's /SLTSL test would pass on a
                // read already in flight, too late for /WAIT; driving the byte
                // then can land on the RAM opcode fetch that follows the poll's
                // read, and the Z80 executes the collision. The disk loop hit
                // this once every few hundred commands; see the same wait in
                // CDiskCartridge::Execute for the timing. Here commands are
                // rare, and the defect is the same.
                (void)save_and_disable_interrupts();
                while (!(gpio_get_all() & SLTSL_GPIO_MASK));
            }
            // 2. Everything else is a short store; /WAIT is released once it is done.
            else {
                // EXCHANGE BUFFER: writes in 0x6000-0x7FFE are kept for the next command.
                if (addr >= BUFFER_START_ADDR && addr <= BUFFER_END_ADDR) {
                    exchange_buffer[addr - BUFFER_START_ADDR] = data;
                }
                else if ((addr >= 0x9000) && (addr < 0xA000)) {
                    if (sccEnabled && (addr >= 0x9800) && (addr < 0xA000)) {
                        _scc.enqueueWrite((uint8_t)(addr & 0xFF), data);
                    }
                    else if ((addr & 0xF800) == 0x9000) {
                        // Mode select only; resetting here would wipe the chip on
                        // every re-select (see CKonamiSCCCartridge::Execute()).
                        sccEnabled = ((data & 0x3F) == 0x3F);
                    }
                }

                releaseWait();
            }

            // Wait for the Z80 write cycle to complete (/WR rising edge).
            while (!(gpio_get_all() & WR_GPIO_MASK));
        }

        // ====================================================================
        // Neither strobe is active yet: drop /WAIT and poll again.
        // ====================================================================
        else {
            releaseWait();
        }
    }

    restore_interrupts(irq_state);
    return true;
}

// ============================================================================
// COMMAND HANDLERS: system commands
// ============================================================================

/**
 * CMD_MAP_FILE (0x01).
 *
 * The Z80 must write a null-terminated filename into the exchange buffer before
 * issuing this command. On success the loader is replaced by the requested ROM;
 * on failure last_cmd_result is set to 0xFE.
 */
void CLoader::CmdMapFile() {
    // Ensure the string is terminated even if the Z80 forgot the trailing zero.
    exchange_buffer[LOADER_BUFFER_SIZE - 1] = '\0';
    
    if(_multiCart.Mount((char*)exchange_buffer))
    {
        _exitFlag = true; // Signal Execute() to return so the new cartridge runs.
        last_cmd_result = 0;
    }
    else
        last_cmd_result = 0xfe;
}

/**
 * CMD_MAP_SCC (0x02).
 *
 * Switches the active cartridge to a bare SCC cartridge with no ROM.
 * Used by the preloader fallback and by loader.ini's scc_on_exit option.
 */
void CLoader::CmdMapSCC() {
    if(_multiCart.MountSCC())
    {
        _exitFlag = true;
        last_cmd_result = 0;
    }
    else
        last_cmd_result = 0xfe;
}

/**
 * CMD_MAP_DISK (0x03).
 *
 * The Z80 writes a null-terminated .dsk path into the exchange buffer, then
 * issues this. MountDisk() does the whole job in one call -- loads and
 * patches disk.rom, opens the image, and swaps the cartridge -- exactly like
 * CmdMapFile() does for a plain ROM. On success the loader is replaced by a
 * disk interface, and the Z80 side soft-resets so the BIOS rescans the slots
 * and finds it: unlike a ROM, a disk interface only gets its INIT called
 * during that scan, not by jumping straight into it.
 */
void CLoader::CmdMapDisk() {
    exchange_buffer[LOADER_BUFFER_SIZE - 1] = '\0';

    // Two optional strings follow the image path, in order: the Disk ROM dump to
    // patch -- for machines whose own dump is not the one named disk.rom -- and
    // the image for the second drive. Either may be empty or absent, which means
    // the default dump and no second disk. Bounded by the buffer, and the
    // terminator above guarantees every walk stops inside it.
    const char* image = (const char*)exchange_buffer;
    const char* rom = nullptr;
    const char* image2 = nullptr;

    size_t n = strlen(image);
    if (n + 1 < LOADER_BUFFER_SIZE - 1) {
        const char* second = image + n + 1;
        if (*second)
            rom = second;

        size_t m = strlen(second);
        if ((size_t)(second - image) + m + 1 < LOADER_BUFFER_SIZE - 1) {
            const char* third = second + m + 1;
            if (*third)
                image2 = third;
        }
    }

    if(_multiCart.MountDisk(image, rom, image2))
    {
        _exitFlag = true;
        last_cmd_result = 0;
    }
    else
        last_cmd_result = 0xfe;
}

// ============================================================================
// COMMAND HANDLERS: file operations
// ============================================================================

/**
 * CMD_FILE_OPEN_READ (0x10).
 *
 * Input:  null-terminated path in exchange_buffer.
 * Output: 4-byte little-endian file size at exchange_buffer[0..3],
 *         buffer opened for reading.
 */
void CLoader::CmdFileOpenRead() {
    exchange_buffer[LOADER_BUFFER_SIZE - 1] = '\0';
    FRESULT res = f_open(&current_file, (char*)exchange_buffer, FA_READ);
    
    if (res == FR_OK) {
        FSIZE_t file_size = f_size(&current_file);
        
        exchange_buffer[0] = (uint8_t)(file_size & 0xFF);
        exchange_buffer[1] = (uint8_t)((file_size >> 8) & 0xFF);
        exchange_buffer[2] = (uint8_t)((file_size >> 16) & 0xFF);
        exchange_buffer[3] = (uint8_t)((file_size >> 24) & 0xFF);
        
        last_cmd_result = 0;
        return_buffer = true;
    } else {
        last_cmd_result = (uint8_t)res;
    }
}

/**
 * CMD_FILE_OPEN_WRITE (0x11).
 *
 * Input: null-terminated path in exchange_buffer.
 * The file is created or truncated. No buffer data is returned.
 */
void CLoader::CmdFileOpenWrite() {
    exchange_buffer[LOADER_BUFFER_SIZE - 1] = '\0';
    FRESULT res = f_open(&current_file, (char*)exchange_buffer, FA_WRITE | FA_CREATE_ALWAYS);
    last_cmd_result = (uint8_t)res;
}

/**
 * CMD_FILE_READ_BLOCK (0x12).
 *
 * Reads up to LOADER_PAYLOAD_MAX bytes from the currently opened file into
 * exchange_buffer[2..]. The actual byte count is stored little-endian at [0..1].
 * The buffer is opened so the Z80 can copy the payload.
 *
 * The cap is the buffer minus four, not minus two: the top two bytes of the
 * window are the command register and the sentinel the Z80's poll reads, so a
 * payload reaching them could not be read back. It used to be minus two, which
 * put the last byte of a maximal block at 0x7FFF and made it unreachable -- a
 * latent defect, since the only block this protocol carries today is
 * /loader.ini at a couple of kilobytes.
 */
void CLoader::CmdFileReadBlock() {
    UINT bytes_read = 0;
    FRESULT res = f_read(&current_file, &exchange_buffer[2], LOADER_PAYLOAD_MAX, &bytes_read);
    
    if (res == FR_OK) {
        exchange_buffer[0] = (uint8_t)(bytes_read & 0xFF);
        exchange_buffer[1] = (uint8_t)((bytes_read >> 8) & 0xFF);
        
        last_cmd_result = 0;
        return_buffer = true;
    } else {
        last_cmd_result = (uint8_t)res;
    }
}

/**
 * CMD_FILE_WRITE_BLOCK (0x13).
 *
 * Input: 2-byte little-endian length at exchange_buffer[0..1],
 *        payload starting at exchange_buffer[2].
 * Output: 0x00 on success, FatFs error code or 0xFE on failure.
 */
void CLoader::CmdFileWriteBlock() {
    UINT bytes_to_write = exchange_buffer[0] | (exchange_buffer[1] << 8);
    UINT bytes_written = 0;
    
    // Clamp the size to what the Z80 can actually have put there. The old cap
    // (buffer minus two) ran the payload up to 0x7FFF, where a Z80 store is a
    // command rather than a buffer write, so the last byte of a maximal block
    // could never arrive -- the mirror of the same defect in CmdFileReadBlock.
    if (bytes_to_write > LOADER_PAYLOAD_MAX) {
        bytes_to_write = LOADER_PAYLOAD_MAX;
    }

    FRESULT res = f_write(&current_file, &exchange_buffer[2], bytes_to_write, &bytes_written);
    
    if (res == FR_OK && bytes_written == bytes_to_write) {
        last_cmd_result = 0;
    } else {
        last_cmd_result = res ? (uint8_t)res : 0xFE;
    }
}

/**
 * CMD_FILE_CLOSE (0x14).
 *
 * Closes the file opened by CmdFileOpenRead or CmdFileOpenWrite and flushes
 * any buffered writes to flash.
 */
void CLoader::CmdFileClose() {
    FRESULT res = f_close(&current_file);
    last_cmd_result = (uint8_t)res;
}

// ============================================================================
// COMMAND HANDLERS: directory operations
// ============================================================================

/**
 * CMD_DIR_OPEN (0x20).
 *
 * Input: null-terminated directory path in exchange_buffer.
 * Opens the directory for iteration by CmdDirReadNext / CmdDirSeek.
 */
void CLoader::CmdDirOpen() {
    exchange_buffer[LOADER_BUFFER_SIZE - 1] = '\0';
    FRESULT res = f_opendir(&current_dir, (char*)exchange_buffer);
    last_cmd_result = (uint8_t)res;
}

/**
 * CMD_DIR_READ_NEXT (0x21).
 *
 * Returns the next visible file or directory entry:
 *   exchange_buffer[0] : 0x00 = file, 0x01 = directory
 *   exchange_buffer[1] : null-terminated entry name
 * Hidden and system files are skipped so the MSX menu stays clean.
 * Returns 0x01 when there are no more entries.
 */
void CLoader::CmdDirReadNext() {
    FRESULT res;
    
    while (true) {
        res = f_readdir(&current_dir, &file_info);
        
        if (res != FR_OK) {
            last_cmd_result = (uint8_t)res;
            return;
        }
        
        if (file_info.fname[0] == 0) {
            last_cmd_result = 0x01; // End Of Directory
            return;
        }
        
        // Skip hidden/system files (e.g. macOS metadata files).
        if (file_info.fattrib & (AM_HID | AM_SYS)) {
            continue; 
        }
        
        break;
    }

    exchange_buffer[0] = (file_info.fattrib & AM_DIR) ? 0x01 : 0x00;

    // snprintf guarantees null-termination and avoids overfilling the buffer.
    snprintf((char*)&exchange_buffer[1], LOADER_BUFFER_SIZE - 1, "%s", file_info.fname);

    // Second string: the 8.3 alias, right after the long name's terminator, and
    // empty when the two are the same. A browser that shows long names has to
    // truncate them to its column, and a truncated long name cannot be opened;
    // the alias always can. FatFs leaves altname empty when fname is already a
    // short name, and the empty string here says exactly that.
    const size_t n = strlen((char*)&exchange_buffer[1]);
    char* alt = (char*)&exchange_buffer[1] + n + 1;
    const size_t room = LOADER_BUFFER_SIZE - 1 - (n + 1);
    snprintf(alt, room, "%s", file_info.altname);

    last_cmd_result = 0;
    return_buffer = true;
}


/**
 * CMD_DIR_SEEK (0x22).
 *
 * Input: 2-byte little-endian index at exchange_buffer[0..1].
 * Rewinds the directory and advances past 'index' visible entries.
 * Returns 0x01 if the index is past the end.
 */
void CLoader::CmdDirSeek() {
    uint16_t target_index = exchange_buffer[0] | (exchange_buffer[1] << 8);

    FRESULT res = f_readdir(&current_dir, NULL);
    if (res != FR_OK) {
        last_cmd_result = (uint8_t)res;
        return;
    }

    for (uint16_t i = 0; i < target_index; ++i) {
        while (true) {
            res = f_readdir(&current_dir, &file_info);

            if (res != FR_OK) {
                last_cmd_result = (uint8_t)res;
                return;
            }

            if (file_info.fname[0] == 0) {
                last_cmd_result = 0x01; // Index out of range
                return;
            }

            if (file_info.fattrib & (AM_HID | AM_SYS)) {
                continue;
            }

            break;
        }
    }

    last_cmd_result = 0x00;
}


/**
 * CMD_DIR_CLOSE (0x23).
 *
 * Closes the directory handle opened by CmdDirOpen.
 */
void CLoader::CmdDirClose() {
    FRESULT res = f_closedir(&current_dir);
    last_cmd_result = (uint8_t)res;
}
