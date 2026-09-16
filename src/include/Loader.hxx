#pragma once

#include "Cartridge.hxx" 
#include "SCCSound.hxx"
#include "ff.h"

// Command/status register at the very end of the 8 KB cartridge page-1 window.
#define CMD_REG_ADDR      0x7FFF

// Second half of the reply, and the whole defence against a bus nobody drives.
//
// A handler runs with /WAIT released, so while one is in flight the cartridge
// answers nothing and the MSX data bus floats at its last driven value -- which
// is the command byte the Z80 itself just wrote. A poll that waits only for
// "0x7FFF is not 0xFF" therefore matches that floating byte and takes it for a
// status: read 0x01 and the preloader reports a failed mount, read 0x00 and it
// jumps through a cartridge vector the firmware has not swapped in yet. Both
// were seen on hardware, and which one you get depends on the last byte driven.
//
// A floating bus is monochromatic -- nothing drives it, so every address reads
// the same held value -- while a driven one is not. So while a result stands
// this address answers LOADER_READY_SENTINEL and 0x7FFF answers the status; a
// poll that requires 0x7FFE == sentinel AND 0x7FFF != sentinel can only be
// satisfied by the firmware actually driving both. No status the loader
// produces is 0xA5 (0x00, 0x01, 0xFE and FatFs codes 0x00-0x13), which is the
// invariant to preserve here. CDiskCartridge uses the same trick with the roles
// of the two addresses swapped; the split is historical, not meaningful.
//
// 0x7FFF still answers the status itself, so a loader.rom built before the gate
// keeps working, ungated, against a firmware that has it.
#define STATUS_REG_ADDR   0x7FFE
#define LOADER_READY_SENTINEL 0xA5

// 8 KB exchange buffer visible to the Z80 at 0x6000-0x7FFE.
//
// Two bytes at the top are not usable as payload: 0x7FFF is the command
// register and 0x7FFE answers the sentinel for as long as a reply stands --
// which is the whole time the Z80 is reading a payload out, because the reply
// repeats until the next command. Hence LOADER_PAYLOAD_MAX: 0x6000..0x7FFD,
// less the two-byte length prefix a block carries.
#define BUFFER_START_ADDR 0x6000
#define BUFFER_END_ADDR   0x7FFE
#define LOADER_BUFFER_SIZE       0x2000
#define LOADER_PAYLOAD_MAX       (LOADER_BUFFER_SIZE - 4)

/**
 * Bootloader / command-cartridge emulation.
 *
 * CLoader exposes a synchronous command protocol to the MSX Z80:
 *   - write a command byte to 0x7FFF
 *   - WAIT is released before the handler runs, because handlers block on file
 *     I/O; the Z80 polls instead of being stalled through the command
 *   - poll until 0x7FFE reads the sentinel and 0x7FFF reads something else;
 *     0x7FFF is then the result/status. The answer repeats until the next
 *     command write, so a lost or duplicated bus cycle costs one more poll
 *     iteration rather than the result itself
 *   - the poll loop must be fetched from MSX RAM, never from this cartridge:
 *     nothing is served while a handler runs
 *   - commands that return data open the 0x6000-0x7FFE exchange buffer
 *
 * It is also responsible for switching the active cartridge to a loaded ROM
 * (CMD_MAP_FILE) or to the bare SCC cartridge (CMD_MAP_SCC).
 */
class CLoader : public CCartridge {
public:
    // `data` selects the image served at 0x4000-0x5FFF; 0 means the embedded
    // preloader. Pass hasRom = false for the bare SCC cartridge, which has no
    // ROM window at all: every read outside the SCC and command windows returns
    // 0xFF, so the MSX BIOS finds no "AB" header and carries on booting to disk
    // or BASIC while SCC I/O stays live.
    CLoader(COmniSCC& multiCart, CSCCSound& scc, const char* data = 0, bool hasRom = true);
    virtual ~CLoader();

    bool Execute() override; 

private:
    // When true, reads from BUFFER_START_ADDR..BUFFER_END_ADDR return exchange_buffer
    // instead of ROM data. Set by commands that need to pass data back to the Z80.
    bool return_buffer;

    // Last command result. 0xFF means "not yet consumed / busy".
    uint8_t last_cmd_result;

    // Active preloader/loader ROM data served at 0x4000-0x5FFF, or 0 for the
    // bare SCC cartridge, which serves no ROM.
    const char *_car_data;

    // Static 8 KB buffer shared with the Z80 through the cartridge window.
    static uint8_t exchange_buffer[LOADER_BUFFER_SIZE];

    // FatFs handles reused across file/directory commands.
    FIL current_file;
    DIR current_dir;
    FILINFO file_info;
    
    CSCCSound& _scc;

    // Protocol command handlers.
    void CmdMapFile();
    void CmdMapSCC();
    void CmdMapDisk();
    void CmdFileOpenRead();
    void CmdFileOpenWrite();
    void CmdFileReadBlock();
    void CmdFileWriteBlock();
    void CmdFileClose();
    void CmdDirOpen();
    void CmdDirReadNext();
    void CmdDirClose();
    void CmdDirSeek();    

};
