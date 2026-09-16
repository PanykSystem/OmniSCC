/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 */

#include "DiskCart.hxx"
#include "OmniSCC.hxx"
#include <string.h>

// Inline assembly macro to embed a binary file into a named linker section.
// The resulting symbol points to the raw bytes and <sym>_size gives the size
// (the .set expression evaluates to a byte count, not an address, because it
// is the difference between two symbols in the same section).
#define IMPORT_BIN(sect, file, sym) asm (\
    ".section " #sect "\n"                  \
    ".balign 4\n"                           \
    ".global " #sym "_start\n"              \
    #sym "_start:\n"                        \
    ".incbin \"" file "\"\n"                \
    ".global " #sym "_size\n"               \
    ".set " #sym "_size, . - " #sym "_start\n" \
    ".balign 4\n"                           \
    ".section \".text\"\n")

// Embed the assembled dispatcher and its relocation table. Both are produced
// by msx/build_stub.py and must be rebuilt whenever diskstub.asm changes.
#ifndef OMNISCC_GENERATED_DIR
#define OMNISCC_GENERATED_DIR "../generated"
#endif
IMPORT_BIN(".rodata.__diskstub", OMNISCC_GENERATED_DIR "/diskstub_ram.bin", __diskstub_ram);
IMPORT_BIN(".rodata.__diskstub", OMNISCC_GENERATED_DIR "/diskstub_ram.rel", __diskstub_ramrel);
IMPORT_BIN(".rodata.__diskstub", OMNISCC_GENERATED_DIR "/diskstub_rom.bin", __diskstub_rom);
IMPORT_BIN(".rodata.__diskstub", OMNISCC_GENERATED_DIR "/diskstub_rom.rel", __diskstub_romrel);
IMPORT_BIN(".rodata.__diskstub", OMNISCC_GENERATED_DIR "/diskstub_win.bin", __diskstub_win);
IMPORT_BIN(".rodata.__diskstub", OMNISCC_GENERATED_DIR "/diskstub_bas.bin", __diskstub_bas);

extern const uint8_t __diskstub_ram_start[];
extern const uint8_t __diskstub_ram_size[];
extern const uint8_t __diskstub_ramrel_start[];
extern const uint8_t __diskstub_win_start[];
extern const uint8_t __diskstub_win_size[];
extern const uint8_t __diskstub_bas_start[];
extern const uint8_t __diskstub_bas_size[];

CDiskCartridge::CDiskCartridge(COmniSCC& multiCart, CSCCSound& scc,
                               uint8_t* patchedRom, uint8_t* exchangeBuf,
                               uint8_t* borrowShadow, CDiskImage& image,
                               CDiskImage& image2, CDiskFiles& files, const RomPatchInfo& info)
    : CCartridge(multiCart, 0, 0), _rom(patchedRom), _buf(exchangeBuf),
      _shadow(borrowShadow), _borrowActive(true),
      _image(image), _image2(image2), _info(info), _scc(scc), _files(files)
{
    return_buffer = false;
    _driverWindow = false;
    _basicWindow = false;
    _dirOpen = false;
    last_cmd_result = 0;
    memset(_buf, 0, DISK_BUF_SIZE);

    // Install the dispatcher where the code window will serve it from. It is
    // assembled at DISK_CODE_START and never relocated, so this is a plain
    // copy, and nothing overwrites it afterwards: a read command fills at most
    // DISK_MAX_SECTORS sectors, which stops below the window.
    const uint32_t winLen = (uint32_t)(uintptr_t)__diskstub_win_size;
    if (winLen <= DISK_CODE_END - DISK_CODE_START + 1)
        memcpy(&_buf[DISK_CODE_START - DISK_BUF_START], __diskstub_win_start,
               winLen);

    // And the Disk BASIC statement handler where the BASIC window will serve
    // it from. Same shape as the dispatcher above, for the same reasons: it is
    // assembled at DISK_BASIC_START, never relocated, and a read command stops
    // below it. BASIC's CALSLT can arrive at any moment after the boot, so it
    // has to be in place from the constructor rather than from a command.
    const uint32_t basLen = (uint32_t)(uintptr_t)__diskstub_bas_size;
    if (basLen <= DISK_BASIC_END - DISK_BASIC_START + 1)
        memcpy(&_buf[DISK_BASIC_START - DISK_BUF_START], __diskstub_bas_start,
               basLen);
}

CDiskCartridge::~CDiskCartridge()
{
    _files.Abort();
}

bool __not_in_flash_func(CDiskCartridge::Execute())
{
    // Interrupts stay disabled for the whole bus loop, including across the two
    // flash handlers: the SDK's flash routines save and restore the mask, so
    // they come back disabled. Most handlers run with /WAIT asserted -- the Z80
    // is stalled on the command write, not polling an unattended bus -- and are
    // bounded in microseconds; CmdDiskWrite and CmdDiskFlush cannot be, so they
    // release the line and the loop waits for /SLTSL to rise before resuming.
    uint32_t const irq_state = save_and_disable_interrupts();

    uint32_t pins;
    uint8_t* rom = _rom;
    uint8_t* buf = _buf;
    bool sccEnabled = false;

    // State of the header-scan sniff (see RomHeaderScanStep), and whether an
    // arm it produced is still waiting for the fetch that would confirm it.
    uint8_t scanState = 0;
    bool armPending = false;

    // The same pair for the statement sniff (see RomStatementScanStep). Its own
    // state byte, because each sniff zeroes its state on any read that is not
    // its own next one: with one byte between them the header step would erase
    // the statement step's progress before the statement step looked at it.
    uint8_t stmtScanState = 0;
    bool basicArmPending = false;

    last_cmd_result = 0xFF;

    while (1) {
        while (((pins = gpio_get_all()) & SLTSL_GPIO_MASK) == SLTSL_GPIO_MASK);

        // Stretch before RD/WR is decoded: an SCC driver writes its mode-select
        // register exactly once, and losing that write hides the chip for the
        // rest of the session.
        forceWait();

        // Hold it. /SLTSL leads /RD and /WR by the best part of a T-state, so
        // the sample that caught the selection usually has no strobe yet. The
        // loop used to fall through to the idle branch, release /WAIT, come
        // straight back round and assert it again -- chopping the line into a
        // burst of pulses for the whole of that lead instead of holding it low.
        //
        // The Z80 samples /WAIT on one clock edge, so a pulse train is how that
        // sample lands in a gap; a missed wait means the Z80 reads the bus
        // before the byte is driven. Spinning with the line held costs nothing:
        // the Z80 is stalled either way, and the wait ends when the byte is
        // driven below. Measured on three machines: no change in behaviour, but
        // it is the correct shape for the signal, so it stays.
        while ((pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK | WR_GPIO_MASK))
               == (RD_GPIO_MASK | WR_GPIO_MASK)) {
            pins = gpio_get_all();
        }

        if (!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK))) {
            uint32_t addr = (pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;
            uint8_t data;

            // The byte has to reach the bus as early as possible. /WAIT is
            // asserted from the /SLTSL edge, but how quickly the MSX actually
            // sees it depends on that machine's bus loading, so the less work
            // there is between the sample and the drive, the more margin the
            // cartridge has on a marginal machine. Everything that does not
            // decide the byte therefore happens AFTER the drive, below, while
            // the Z80 is still finishing the cycle.
            //
            // Two things dominate the traffic and both are timing-critical:
            // instruction fetches, because the Z80 runs the whole of Disk BASIC
            // out of this ROM, and the sector bytes a CopyOut LDIR reads.

            // A pending arm is the one piece of bookkeeping that cannot wait:
            // the fetch it is waiting for is the stub's own first byte, and
            // answering it with the vendor byte would drop the Z80 one
            // instruction into the wrong code. It is a bool test on every other
            // cycle. 0x4022 is live Disk BASIC, so the swap only happens on a
            // read of borrowAddr that a header scan armed -- see below.
            if (armPending) {
                armPending = false;
                if (addr == _info.borrowAddr && !_borrowActive) {
                    CRomPatcher::SwapBorrow(rom, _shadow, _info);
                    _borrowActive = true;
                }
            }

            // The armed fetch is the entry BASIC's CALSLT jumps to, so the
            // window has to be open for this very cycle. Like armPending, the
            // arm lives exactly one cycle: between BASIC reading 0x4005 and
            // entering us, only RDSLT and CALSLT run, and neither of them
            // touches this cartridge.
            if (basicArmPending) {
                basicArmPending = false;
                if (addr == DISK_BASIC_ENTRY)
                    _basicWindow = true;
            }

            if (addr >= DISK_BUF_START && addr < DISK_STATUS_REG &&
                (return_buffer ||
                 (_driverWindow && addr >= DISK_CODE_START) ||
                 (_basicWindow && addr >= DISK_BASIC_START &&
                  addr <= DISK_BASIC_END))) {
                data = buf[addr - DISK_BUF_START];
            }
            else if (addr >= DISK_ROM_START && addr < DISK_SPECIAL_START) {
                // The common case, and now a single compare away from the bus:
                // a plain ROM byte. Everything that answers as something other
                // than ROM or buffer lives at or above DISK_SPECIAL_START.
                data = rom[addr - DISK_ROM_START];
            }
            else {
                data = 0xFF;

                // 0x7FFF answers as the command register and 0x7FFE as the
                // status while a result is ready, and they must differ (token
                // vs a non-token status) so the MSX-side poll can tell a driven
                // bus from a floating one -- last_cmd_result is never
                // DISK_READY_TOKEN. See DISK_READY_TOKEN.
                if (addr == 0x7FFF && last_cmd_result != 0xFF) {
                    data = DISK_READY_TOKEN;
                }
                else if (addr == DISK_STATUS_REG && last_cmd_result != 0xFF) {
                    data = last_cmd_result;
                }
                else if (return_buffer && addr >= DISK_BUF_START &&
                         addr <= DISK_BUF_END) {
                    data = buf[addr - DISK_BUF_START];
                }
                else if (DISK_FDC_FILL_ENABLED && _info.fdcAddr &&
                         addr >= _info.fdcAddr &&
                         addr < _info.fdcAddr + DISK_FDC_LEN) {
                    // Sits below the exchange-buffer branch on purpose: while a
                    // command has the overlay open these addresses are buffer.
                    data = DISK_FDC_FILL;
                }
                else if (sccEnabled && addr >= 0x9800 && addr < 0xA000) {
                    data = _scc.readRegister((uint8_t)addr);
                }
                else if (addr >= DISK_ROM_START && addr <= DISK_ROM_END) {
                    data = rom[addr - DISK_ROM_START];
                }
            }

            gpio_put_masked(DATA_GPIO_MASK, (uint32_t)data);
            SET_DATA_MODE_OUT;
#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 1);
#endif
            releaseWait();

            // Off the critical path: the byte is already on the bus and the Z80
            // is still finishing this cycle. Re-arm the borrow window on the
            // BIOS's header scan, so a reset finds the stub again -- the Pico is
            // not reset with the MSX. The arm is consumed on the next cycle, at
            // the top of the loop, which is where the swap happens.
            if (RomHeaderScanStep(scanState, (uint16_t)addr, false)) {
                armPending = true;
                // The BIOS is scanning cartridge headers, so the MSX has just
                // been reset. A reset taken inside a disk call would otherwise
                // leave the code window open forever, hiding live vendor bytes
                // from Disk BASIC. The BASIC window hides its own bytes just
                // below, and a reset taken inside a statement would strand it
                // the same way.
                _driverWindow = false;
                _basicWindow = false;

                // A reset taken inside CALL DIR strands the listing's
                // directory as well: the window close that ends every
                // statement is what would have closed it, and it is never
                // sent. The handle is dropped here rather than closed, because
                // f_closedir lives in flash and this is the bus loop -- an XIP
                // stall on the cycle after a driven byte is how the arm above
                // misses the fetch it exists for. Dropping it does the whole of
                // what the flag is for: a step that arrives with no open behind
                // it is refused, and the next CMD_BASIC_DIR_OPEN builds the
                // handle again from nothing.
                _dirOpen = false;
            }

            // BASIC's CALL dispatch, with our STATEMENT vector just read
            // (0x4004 then 0x4005, measured -- see RomStatementScanStep) and a
            // CALSLT into it next. The arm is consumed on the following cycle,
            // at the top of the loop, which is the fetch of the handler's
            // first byte -- and opening the window is all it does, since the
            // byte for that cycle is decided after it. The BIOS's boot-time
            // SLTATR fill reads the same pair and arms this too; its next
            // read is 0x4006, which drops the arm with nothing installed.
            if (RomStatementScanStep(stmtScanState, (uint16_t)addr, false))
                basicArmPending = true;

            while (!(gpio_get_all() & RD_GPIO_MASK));

#ifdef WITH_SHIFTERS
            gpio_put(DATADIR_PIN, 0);
#endif
            SET_DATA_MODE_IN;
        }
        else if (!(pins & (SLTSL_GPIO_MASK | WR_GPIO_MASK))) {
            uint8_t data = pins & DATA_GPIO_MASK;
            uint32_t addr = (pins & ADDR_GPIO_MASK) >> ADDR_GPIO_SHIFT;

            // A write is not the stub fetch a pending arm is waiting for,
            // and it breaks any header-scan sequence in progress. Nothing was
            // installed on the arm, so dropping it is the whole of the work.
            (void)RomHeaderScanStep(scanState, (uint16_t)addr, true);
            armPending = false;
            (void)RomStatementScanStep(stmtScanState, (uint16_t)addr, true);
            basicArmPending = false;

            if (addr == 0x7FFF) {
                return_buffer = false;
                last_cmd_result = 0xFF;

                // Every command but the two flash ones runs to completion with
                // /WAIT still asserted, so the Z80 stays stalled on this write
                // cycle until the result is ready and cannot fetch a byte the
                // firmware has not driven yet. That is the fix for the read
                // path: dropping /WAIT there, while the driver's poll loop was
                // itself served from this cartridge, left the bus unattended for
                // the length of the handler and the Z80 latched garbage -- a
                // per-command crash that compounded over a long load.
                //
                // Holding it is safe only because a read is short: the driver
                // chunks each transfer to CHUNK_SECTORS sectors (see
                // diskstub.asm), so one hold cannot stall the Z80 -- and thus
                // starve MSX DRAM refresh -- long enough to rot RAM.
                // DISK_MAX_SECTORS here is just the buffer's hard cap. A flash
                // write cannot be made short, so those two commands release the
                // line and are polled for from MSX RAM instead; see
                // CmdDiskWrite. Interrupts stay disabled throughout either way,
                // like the rest of the loop.
                // Set by the two handlers that let the Z80 go before they
                // finish, because the loop cannot simply resume after one.
                bool busReleased = false;

                switch (data) {
                case 0x00: last_cmd_result = 0;   break;    // close buffer
                case 0x31: CmdDiskRead();         break;
                // The two that touch flash. Both release /WAIT themselves, as
                // early as they can: see CmdDiskWrite. Everything else in this
                // switch is answered with the line still held.
                case 0x32: CmdDiskWrite();  busReleased = true; break;
                case 0x34: CmdDiskFlush();  busReleased = true; break;
                case 0x39: CmdDiskFormat(); busReleased = true; break;
                case 0x33: CmdDiskStatus();       break;
                case 0x35: CmdDiskSetRam();       break;
                case 0x36: CmdDiskUnborrow();     break;
                // Open and close the code window. Both are a flag and nothing
                // else, so the write that carries them is the whole handshake:
                // /WAIT is still asserted here, and the Z80's next fetch --
                // which for WINDOW_ON is the call into the window itself --
                // happens after this returns.
                case 0x37: _driverWindow = true;  last_cmd_result = 0; break;
                case 0x38: _driverWindow = false; last_cmd_result = 0; break;
                // Close the BASIC window. There is no matching open: the bus
                // loop opens it itself, on the armed fetch of the entry point,
                // because BASIC gets there through CALSLT and has no chance to
                // ask for it first. Like 0x37/0x38 it is a flag and nothing
                // else, so the write is the whole handshake -- /WAIT is still
                // asserted, and the Z80's next fetch happens after this
                // returns, by which time the vendor bytes are back.
                // CloseBasicDir() beside it because this write is the one thing
                // every statement does on its way out, whether it finished or
                // gave up: a listing that stopped on an error has a directory
                // open and no other chance to close it. It stays on the fast
                // path with /WAIT held, which is the rule for anything answered
                // here -- a statement that opened nothing pays a bool test, and
                // f_closedir touches no media at all: it invalidates the
                // handle and nothing else.
                case 0x3A: _basicWindow = false;  CloseBasicDir();
                           last_cmd_result = 0; break;
                // CALL MOUNT and CALL EJECT. Both open or close a file, which
                // is file I/O and cannot run with the Z80 stalled, so both
                // release /WAIT and are polled for from MailCmd in MSX RAM --
                // the same regime as the flash commands above.
                case 0x3B: CmdBasicMount(); busReleased = true; break;
                case 0x3C: CmdBasicEject(); busReleased = true; break;
                // CALL DIR's pair, and both belong with the mount rather than
                // with the info below: opening a directory walks the path's
                // components and stepping one scans directory sectors past
                // whatever is hidden in them, neither of which is bounded in
                // microseconds the way a capped sector read is.
                case 0x3D: CmdBasicDirOpen(); busReleased = true; break;
                case 0x3E: CmdBasicDirNext(); busReleased = true; break;
                // CALL DISK. Not one of those: it reads four accessors off two
                // objects already in RAM and copies two short paths, so it is
                // answered on the fast path with the line still held.
                case 0x3F: CmdBasicInfo();        break;
                case 0x40: case 0x41: case 0x42: case 0x43:
                    CmdFile(data); busReleased = true; break;
                default:   last_cmd_result = 0xFE; break;
                }

                // The line goes first, and only then does the loop wait for
                // the cycle to end.
                //
                // That order is load-bearing. The three flash commands are
                // expected to release /WAIT inside the handler, but every one of
                // them can answer before it gets there -- a drive with no disk,
                // a write-protected image, a bad sector count -- and those exits
                // leave the line asserted. Waiting for /SLTSL to rise with
                // /WAIT still down is a deadlock: the Z80 is stalled inside the
                // write cycle that carried the command, so it cannot finish the
                // cycle that would raise /SLTSL, and nothing else will. It hung
                // the machine on the first CALL FORMAT that reached an empty
                // drive B. releaseWait() on an already-released line is a
                // no-op, so the handlers that do release lose nothing.
                releaseWait();

                // Back to tight bus polling -- but not before the bus is
                // between cycles. A handler that let the Z80 go left it polling
                // 0x7FFF unattended, so the loop top's /SLTSL test would pass
                // on a read already in flight, too late for /WAIT; driving the
                // byte then can land on the opcode fetch from MSX RAM that
                // follows the poll's read, with the cartridge and the RAM both
                // driving and the Z80 latching the collision as an instruction.
                // Waiting for /SLTSL to rise turns a cycle caught in flight
                // into a cycle missed cleanly, which the poll absorbs as one
                // more iteration. CLoader::Execute carries the same wait after
                // its commands, for the same reason -- it was measured there at
                // once every few hundred commands.
                if (busReleased) {
                    while (!(gpio_get_all() & SLTSL_GPIO_MASK));
                }
            }
            else {
                const bool fdcReg = _info.fdcAddr &&
                                    addr >= _info.fdcAddr &&
                                    addr < _info.fdcAddr + DISK_FDC_LEN;

                // A write to the vendor's controller registers goes to the
                // controller, which is not here -- so it goes nowhere, the way
                // it would on any interface whose FDC is absent. Capturing it
                // into the exchange buffer instead would put vendor bytes
                // wherever that window happens to land in the buffer.
                if (addr >= DISK_BUF_START && addr <= DISK_BUF_END && !fdcReg) {
                    buf[addr - DISK_BUF_START] = data;
                }
                else if (addr >= 0x9000 && addr < 0xA000) {
                    if (sccEnabled && addr >= 0x9800) {
                        _scc.enqueueWrite((uint8_t)(addr & 0xFF), data);
                    }
                    else if ((addr & 0xF800) == 0x9000) {
                        // Mode select only. Resetting here would wipe the chip
                        // on every re-select.
                        sccEnabled = ((data & 0x3F) == 0x3F);
                    }
                }
                releaseWait();
            }

            while (!(gpio_get_all() & WR_GPIO_MASK));
        }
        else {
            releaseWait();
        }
    }

    restore_interrupts(irq_state);
    return true;
}

/**
 * CMD_DISK_UNBORROW (0x36).
 *
 * Hand the borrow window back to the vendor. Issued by the INIT tail as its
 * first act once it is running from MSX RAM, before a single vendor
 * instruction has run, so from here on the served image is byte-identical to
 * the dump apart from the jump table thunks and the INIT vector. It has to
 * happen this early: the vendor INIT executes code inside the window.
 *
 * The swap is its own inverse, and _borrowActive keeps a repeated command --
 * or a repeated arm in the bus loop -- from swapping the stub back in on top
 * of live vendor code.
 *
 * Non-blocking, like CmdDiskSetRam: no file I/O here, ever.
 */
void CDiskCartridge::CmdDiskUnborrow()
{
    if (_borrowActive) {
        CRomPatcher::SwapBorrow(_rom, _shadow, _info);
        _borrowActive = false;
    }
    last_cmd_result = 0;
}

/**
 * CMD_DISK_READ (0x31).
 *
 * Parameters at DISK_P_*. Sector data is returned at the base of the exchange
 * buffer so the Z80 side is a single LDIR.
 */
/**
 * The image a command is addressed to.
 *
 * Every disk command carries the drive number the Disk BIOS was called with, in
 * DISK_P_DRIVE. Drive 0 is loader.ini's last=, drive 1 its disk2=; the driver
 * refuses anything higher before it gets here, and an unmounted image comes back
 * as null so the handlers can answer "not ready" instead of serving drive A's
 * sectors under a second letter.
 */
CDiskImage* CDiskCartridge::DriveImage()
{
    CDiskImage* image = DriveSlot();
    return (image && image->Mounted()) ? image : 0;
}

/**
 * The drive a command names, whether or not it holds a disk.
 *
 * DriveImage() answers null for an empty drive, which is what every sector
 * command wants: there is nothing there to read or write. CALL MOUNT and CALL
 * EJECT are about the drive rather than about the disk in it -- an empty drive
 * is exactly the one a mount is aimed at -- so they ask for the drive itself.
 * Null here means there is no such drive at all, which the driver already
 * refuses on its own side with "NO SUCH DRIVE"; this is what keeps a drive byte
 * that arrived some other way from indexing past the two images.
 */
CDiskImage* CDiskCartridge::DriveSlot()
{
    return DriveSlot(_buf[DISK_P_DRIVE]);
}

/**
 * The same, by number rather than out of the parameter block.
 *
 * CMD_BASIC_INFO reports both drives in one reply, so it has no single drive
 * byte to ask about. It goes through here rather than reaching for _image and
 * _image2 itself, so that the drive -> image mapping exists once: a second copy
 * would be free to disagree, and disagreeing means serving drive A's image
 * under drive B's letter.
 */
CDiskImage* CDiskCartridge::DriveSlot(uint8_t drive)
{
    switch (drive) {
        case 0: return &_image;
        case 1: return &_image2;
    }
    return 0;
}

void CDiskCartridge::CmdDiskRead()
{
    const uint32_t lba =
        (uint32_t)_buf[DISK_P_LBA_LO] |
        ((uint32_t)_buf[DISK_P_LBA_MID] << 8) |
        ((uint32_t)_buf[DISK_P_LBA_HI] << 16);
    uint8_t count = _buf[DISK_P_COUNT];

    // Any error answered here reaches the MSX as a DSKIO failure, and some
    // disks install a DISKVE handler that turns one straight into "abort".
    if (count == 0 || count > DISK_MAX_SECTORS) {
        last_cmd_result = 0xFE;
        return;
    }

    CDiskImage* image = DriveImage();
    if (!image) {
        last_cmd_result = DISK_ST_NOTREADY;
        return;
    }

    if (!image->Read(lba, count, _buf)) {
        last_cmd_result = 0x04;     // data error, mapped to DSKIO code 4
        return;
    }

    return_buffer = true;
    last_cmd_result = 0;
}

/**
 * CMD_DISK_WRITE (0x32).
 *
 * Parameters at DISK_P_*, sector data already at the base of the exchange
 * buffer: buffer-range writes are captured on every bus cycle, so the driver
 * fills it with a plain LDIR and then sends this command.
 *
 * This is the one command that must not be answered with /WAIT held. A stalled
 * Z80 issues no M1 cycles and so refreshes no MSX DRAM, which bounds a held
 * line at roughly 2 ms; one 512-byte page program is most of a millisecond,
 * every eighth of them erases a 4 KB flash sector, and an erase is tens of
 * milliseconds. So the line goes as soon as the parameters are known to be
 * good, and the driver polls for the result from MailCmd, which lives in MSX
 * RAM -- the part that makes this safe, because for the length of this handler
 * the cartridge serves no bus at all and a poll loop in the code window would
 * be fetching its own instructions from an undriven one. That distinction is
 * the whole difference between this and the read path, where releasing /WAIT
 * crashed the Z80 once per command.
 *
 * If the released line ever proves to be trouble on hardware, the fallback is
 * to hold it and cap the write at a single sector -- which leaves the eighth
 * sector's erase stalling the Z80 for tens of milliseconds, so it is a
 * fallback, not a fix.
 */
void CDiskCartridge::CmdDiskWrite()
{
    const uint32_t lba =
        (uint32_t)_buf[DISK_P_LBA_LO] |
        ((uint32_t)_buf[DISK_P_LBA_MID] << 8) |
        ((uint32_t)_buf[DISK_P_LBA_HI] << 16);
    const uint8_t count = _buf[DISK_P_COUNT];

    if (count == 0 || count > DISK_MAX_SECTORS) {
        last_cmd_result = 0xFE;
        return;
    }

    CDiskImage* image = DriveImage();
    if (!image) {
        last_cmd_result = DISK_ST_NOTREADY;
        return;
    }

    // Answered with the line still held: it is a flag test, and the driver
    // turns this one status into DSKIO's "write protected" rather than into a
    // write fault.
    if (image->ReadOnly()) {
        last_cmd_result = DISK_ST_WRPROT;
        return;
    }

    releaseWait();

    if (!image->Write(lba, count, _buf)) {
        last_cmd_result = 0x04;
        return;
    }

    last_cmd_result = 0;
}

/**
 * CMD_DISK_FLUSH (0x34).
 *
 * Sent once at the end of a write call, not once per chunk. The sector data is
 * already in flash by then; what this persists is the filesystem's own map of
 * where it put it, which otherwise exists only in the Pico's RAM -- along with
 * the fact that the pages the write replaced are free. Lose that and the image
 * comes back with neither the new data nor the old.
 *
 * The map lives in one 4 KB flash sector, so this erases it: milliseconds, and
 * /WAIT goes first for the same reason as CmdDiskWrite. Syncing per chunk
 * instead of per call would erase that one sector once per 512 bytes written.
 */
void CDiskCartridge::CmdDiskFlush()
{
    CDiskImage* image = DriveImage();
    if (!image) {
        last_cmd_result = DISK_ST_NOTREADY;
        return;
    }

    releaseWait();
    last_cmd_result = image->Sync() ? 0 : 0x04;
}

/**
 * CMD_DISK_FORMAT (0x39).
 *
 * Writes a fresh filesystem over the mounted image -- boot sector, both FATs,
 * empty root directory -- and raises the disk-changed flag so the next DSKCHG
 * sends the caller back to GETDPB for the geometry this just wrote.
 *
 * The exchange buffer is the scratch sector. It is ours for the length of the
 * command and only the first 512 bytes are used, well below the code window.
 *
 * Answered with /WAIT released, like every command that touches flash. A format
 * is a handful of sector writes rather than a scrub of the whole image: the data
 * area is left alone, exactly as a real floppy format leaves it.
 *
 * One status for every failure except write protection, because the Z80 has
 * nothing different to do about them: a size whose BPB this project will not
 * invent (see DskGeometryFor) and a write that failed both come back as
 * DSKFMT's "other error".
 */
void CDiskCartridge::CmdDiskFormat()
{
    CDiskImage* image = DriveImage();
    if (!image) {
        last_cmd_result = DISK_ST_NOTREADY;
        return;
    }

    if (image->ReadOnly()) {
        last_cmd_result = DISK_ST_WRPROT;
        return;
    }

    releaseWait();

    last_cmd_result = image->Format(_buf) ? 0 : 0x04;
}

/**
 * CMD_DISK_STATUS (0x33).
 *
 * Bit 0 of DISK_P_FLAGS is "changed since last query", bit 1 is write
 * protected -- true for an image the volume would not open for writing, which
 * is this cartridge's write-protect notch.
 */
void CDiskCartridge::CmdDiskStatus()
{
    CDiskImage* image = DriveImage();
    if (!image) {
        // An empty drive is not a failed command: DSKCHG asks about a drive and
        // "there is no disk in it" is an answer. The driver maps this status to
        // not ready, which is what the Disk BIOS expects for an empty drive.
        last_cmd_result = DISK_ST_NOTREADY;
        return;
    }

    uint8_t flags = 0;
    if (image->TakeChangedFlag())
        flags |= 0x01;
    if (image->ReadOnly())
        flags |= 0x02;

    _buf[DISK_P_FLAGS] = flags;
    return_buffer = true;
    last_cmd_result = 0;
}

/**
 * CMD_DISK_SETRAM (0x35).
 *
 * The INIT tail reports where it reserved MSX RAM. Point the patched jump
 * table entries at the dispatcher that will live there, then leave a copy of
 * the dispatcher blob -- relocated for that RAM address -- at the base of the
 * exchange buffer and open the overlay, so the tail can LDIR it out.
 *
 * The buffer is safe to use here where it was not before: the tail runs from
 * page 3, so an overlay covering page 1 cannot answer its instruction
 * fetches. That is what let the borrow window shrink to the ROM part alone --
 * the dispatcher no longer has to be smuggled through the ROM image.
 *
 * No file I/O here, ever: this runs on the fast path, with /WAIT asserted.
 */
void CDiskCartridge::CmdDiskSetRam()
{
    // The handshake is the guard. Vendor INIT code -- which now runs before
    // this command rather than after it -- writes to what it believes are FDC
    // registers, and 0x7FFF is one of them on plenty of interfaces; honouring
    // a stray 0x35 would repoint the jump table and relocate the dispatcher
    // for whatever happened to be in the parameter bytes. Clearing the magic
    // makes the command one-shot per handshake as well.
    if (_buf[DISK_P_MAGIC0] != DISK_SETRAM_MAGIC0 ||
        _buf[DISK_P_MAGIC1] != DISK_SETRAM_MAGIC1) {
        last_cmd_result = 0xFE;
        return;
    }
    _buf[DISK_P_MAGIC0] = 0;
    _buf[DISK_P_MAGIC1] = 0;

    const uint16_t base =
        (uint16_t)_buf[DISK_P_LBA_LO] |
        ((uint16_t)_buf[DISK_P_LBA_MID] << 8);

    // The dispatcher sits at the base of the reserved area: there is nothing
    // in front of it any more. The page-1 transfer path used to need a bounce
    // buffer there and now stages on the MSX stack instead, because every byte
    // of this reservation comes out of what MSX BASIC has left below HIMEM and
    // a disk whose BLOAD image ends just under BASIC's stack is destroyed by
    // the difference. See HOP_SIZE in msx/diskstub.asm.
    const uint16_t dispAddr = base;

    CRomPatcher::SetDispatcher(_rom, _info, dispAddr);

    const uint16_t len = (uint16_t)(uintptr_t)__diskstub_ram_size;
    memcpy(_buf, __diskstub_ram_start, len);
    CRomPatcher::Relocate(_buf, len, __diskstub_ramrel_start, dispAddr);

    // The twin of OrigInitAddr: the STATEMENT address the vendor's own ROM
    // header published, into the cell that is this blob's last two bytes.
    // build_stub.py's 5A A5 guard is what makes "the last two bytes" a safe
    // way to address it -- nothing may be appended after the cell.
    //
    // Not a relocation site, so it is written after Relocate rather than
    // before it: the value comes from the dump, not from the blob's assembly
    // origin. The statement blob reads it from here to chain a name that is
    // not ours to the vendor's handler, which is what keeps CALL FORMAT and
    // CALL SYSTEM working. Zero when the dump published no extended statements
    // at all, and the blob then refuses the name rather than jumping to 0.
    if (len >= 2) {
        _buf[len - 2] = (uint8_t)(_info.originalStatement & 0xFF);
        _buf[len - 1] = (uint8_t)(_info.originalStatement >> 8);
    }

    return_buffer = true;
    last_cmd_result = 0;
}

/**
 * CMD_BASIC_MOUNT (0x3B).
 *
 * CALL MOUNT("[X:]path"): put a different image in a drive without rebooting
 * the machine. The path is NUL-terminated at the base of the exchange buffer
 * and the drive is in DISK_P_DRIVE; the driver has already taken the "A:"/"B:"
 * prefix off the path and refused any other letter, so a drive number that is
 * neither 0 nor 1 cannot come from a driver of ours and is refused here rather
 * than trusted.
 *
 * Nothing has to be invalidated on either side. Mount() replaces whatever was
 * mounted and raises the disk-changed flag, so the next DSKCHG tells Disk BASIC
 * the media changed and it re-reads the DPB from the new image's own boot
 * sector -- which is how a 360 KB image can replace a 720 KB one mid-session.
 *
 * The path is checked BEFORE Mount() is called, because Mount() starts with
 * Unmount(): a typo in a name would otherwise empty the drive and then report
 * the typo, and the disk a program was running from is gone. f_stat is what
 * Mount() would have found out the hard way -- the file exists, is a file and
 * not a directory, and is a non-zero whole number of 512-byte sectors -- and
 * a path that fails any of it answers DISK_ST_NOFILE with the drive untouched.
 * The same three conditions Mount() itself enforces, checked here rather than
 * moved into CDiskImage, whose Mount() the loader's boot path shares. Between
 * the stat and the open the file could still change underneath; nothing on
 * the MSX side can make that happen, and the answer then is a failed Mount()
 * with an empty drive, which is the pre-existing behaviour and not a new one.
 *
 * Answered with /WAIT released, like the flash commands and for the same
 * reason: this opens a file and builds a CLMT, which is unbounded file I/O
 * against a stalled Z80's ~2 ms DRAM refresh budget. The driver polls for the
 * result from MailCmd in MSX RAM -- never from a window, because for the length
 * of this handler the cartridge serves no bus at all. See CmdDiskWrite.
 */
void CDiskCartridge::CmdBasicMount()
{
    CDiskImage* image = DriveSlot();
    if (!image) {
        last_cmd_result = DISK_ST_NOTREADY;
        return;
    }

    // Bounded by the count, not by the terminator. Every byte of the exchange
    // buffer is writable from the bus and none of it is written by us, so
    // "copy until the NUL" is a copy whose length the Z80 side decides -- and
    // the destination is 65 bytes of this core's stack. The driver refuses a
    // longer path before it sends the command, so anything that fills this is
    // already a path nothing could have opened.
    char path[DISK_PATH_MAX];
    uint32_t len = 0;
    while (len < DISK_BASIC_PATH_MAX && _buf[len]) {
        path[len] = (char)_buf[len];
        ++len;
    }
    path[len] = '\0';

    releaseWait();

    // One status for every way an image can fail to arrive. A name with no file
    // behind it, a directory, and a file that is not a whole number of 512-byte
    // sectors are the same answer to whoever typed it -- that is not a disk
    // this drive can take -- and Mount() does not distinguish them either. An
    // empty path is refused here too: f_stat on "" answers FR_INVALID_NAME,
    // where Mount() would have unmounted first and then failed to open it.
    FILINFO info;
    if (f_stat(path, &info) != FR_OK || (info.fattrib & AM_DIR) ||
        info.fsize == 0 || (info.fsize % DSK_SECTOR_SIZE) != 0) {
        last_cmd_result = DISK_ST_NOFILE;
        return;
    }

    last_cmd_result = image->Mount(path) ? 0 : DISK_ST_NOFILE;
}

/**
 * CMD_BASIC_EJECT (0x3C).
 *
 * CALL EJECT(["X:"]): leave the drive empty. Sector commands for it then answer
 * DISK_ST_NOTREADY, which the driver turns into DSKIO's "not ready" -- the same
 * thing drive B is whenever loader.ini names no disk2=.
 *
 * Ejecting a drive that is already empty is not an error: the statement asks
 * for a state, not for a transition, and Unmount() on an unmounted image is a
 * documented no-op. Only a drive that does not exist at all is refused.
 *
 * /WAIT goes first for the same reason as the mount: Unmount() closes the file,
 * and closing a file it wrote to flushes it.
 */
void CDiskCartridge::CmdBasicEject()
{
    CDiskImage* image = DriveSlot();
    if (!image) {
        last_cmd_result = DISK_ST_NOTREADY;
        return;
    }

    releaseWait();

    image->Unmount();
    last_cmd_result = 0;
}

/**
 * CMD_BASIC_INFO (0x3F).
 *
 * CALL DISK: what is in each drive, in one reply. Two records at the base of the
 * exchange buffer, in drive order, each of them a state byte (bit 0 mounted,
 * bit 1 write-protected), the sector count as a little-endian word, and the path
 * with its NUL. An empty drive gets a full record too -- state 0, count 0, an
 * empty path -- because the driver walks one record to reach the next, and a
 * record that could be short would need a length to walk past instead.
 *
 * Answered on the fast path, with /WAIT still asserted, unlike the mount and the
 * eject beside it. Everything here is already in RAM: Mounted(), ReadOnly() and
 * SectorCount() read members, and Path() hands back the string Mount() kept
 * because a FIL holds no name of its own. Two paths of at most
 * DISK_BASIC_PATH_MAX bytes is the whole of the work, which keeps this inside
 * the rule for anything served with the Z80 stalled: bounded, free of file I/O,
 * low-microsecond.
 *
 * The count is clamped to what its 16-bit field holds rather than wrapped. The
 * sizes this exists for are 720 and 1440 sectors, but CDiskImage::Mount
 * whitelists no size at all -- any file that is a whole number of 512-byte
 * sectors mounts -- and a wrapped count would read on screen as a plausible
 * small disk, which is worse than one obviously pegged at the top.
 *
 * The overlay this opens is closed by the statement's own window close: that is
 * a write to 0x7FFF, which clears return_buffer, and every path out of the
 * handler takes it. Leaving it open while the statement runs is safe for the
 * reason CmdBasicDirNext states in full below -- both branches of the read path
 * resolve to the same byte of _buf, so an open overlay is byte-for-byte
 * unobservable to the blob being served out of it.
 */
void CDiskCartridge::CmdBasicInfo()
{
    uint32_t at = 0;
    for (uint8_t drive = 0; drive < 2; ++drive) {
        CDiskImage* image = DriveSlot(drive);
        const bool mounted = image && image->Mounted();

        uint32_t sectors = mounted ? image->SectorCount() : 0;
        if (sectors > 0xFFFF)
            sectors = 0xFFFF;

        _buf[at++] = (uint8_t)((mounted ? 1 : 0) |
                               (mounted && image->ReadOnly() ? 2 : 0));
        _buf[at++] = (uint8_t)(sectors & 0xFF);
        _buf[at++] = (uint8_t)(sectors >> 8);

        // Bounded by the same count the mount accepts, not by the terminator:
        // _path is that long at most, and a bound is what keeps this a fixed
        // amount of work rather than a walk whose length something else decides.
        const char* path = mounted ? image->Path() : "";
        for (uint32_t i = 0; i < DISK_BASIC_PATH_MAX && path[i]; ++i)
            _buf[at++] = (uint8_t)path[i];
        _buf[at++] = 0;
    }

    return_buffer = true;
    last_cmd_result = 0;
}

/**
 * Close the listing's directory, if one is open.
 *
 * Called from the two places a listing can end -- the step that walks off the
 * end of the directory, and the window close every statement sends on its way
 * out, which is what covers a listing that stopped on an error. Also from
 * CmdBasicDirOpen, so a second listing cannot inherit the first one's handle.
 *
 * Not called from the bus loop's reset branch, which drops the flag instead:
 * f_closedir is flash-resident, and an XIP stall there would be taken on the
 * cycle after a byte was driven, where the borrow window's re-arm is waiting
 * for the very next fetch.
 */
void CDiskCartridge::CloseBasicDir()
{
    if (_dirOpen) {
        f_closedir(&_dir);
        _dirOpen = false;
    }
}

/**
 * CMD_BASIC_DIR_OPEN (0x3D).
 *
 * CALL DIR(["path"]): open a directory of the cartridge's own FAT volume so
 * that CMD_BASIC_DIR_NEXT can step it. The path is NUL-terminated at the base
 * of the exchange buffer and empty for the root; no command has to open the
 * buffer to put it there, because buffer-range writes are captured on every bus
 * cycle.
 *
 * This is about the volume the cartridge boots from, not about either drive, so
 * no drive byte comes into it: the images in A and B are files *on* this
 * volume, and a listing is how a user finds out what they could mount.
 *
 * Answered with /WAIT released, like the mount: f_opendir walks the path one
 * component at a time and each component is a linear scan of a directory, which
 * on a volume with hundreds of entries is nothing like the bounded, file-I/O
 * free work the fast path is for. The driver polls for it from MailCmd in MSX
 * RAM -- never from a window, because for the length of this handler the
 * cartridge serves no bus at all. See CmdDiskWrite.
 *
 * One status for every way it can fail, and it is the one a missing file gets:
 * a name with no directory behind it and a name that is a file rather than a
 * directory are the same answer to whoever typed it.
 */
void CDiskCartridge::CmdBasicDirOpen()
{
    // Bounded by the count, not by the terminator, exactly as CmdBasicMount
    // copies its path: every byte of the exchange buffer is writable from the
    // bus and none of it is written by us, so "copy until the NUL" would be a
    // copy whose length the Z80 side decides into 65 bytes of this core's
    // stack. The driver refuses a longer path before it sends the command.
    char path[DISK_PATH_MAX];
    uint32_t len = 0;
    while (len < DISK_BASIC_PATH_MAX && _buf[len]) {
        path[len] = (char)_buf[len];
        ++len;
    }
    path[len] = '\0';

    releaseWait();

    // A listing that stopped early leaves its handle behind until the window
    // close collects it; a second CALL DIR that arrives first must not inherit
    // it, and f_opendir would overwrite the object either way.
    CloseBasicDir();

    if (f_opendir(&_dir, path) != FR_OK) {
        last_cmd_result = DISK_ST_NOFILE;
        return;
    }

    _dirOpen = true;
    last_cmd_result = 0;
}

/**
 * CMD_BASIC_DIR_NEXT (0x3E).
 *
 * One entry from the directory CMD_BASIC_DIR_OPEN opened, at the base of the
 * exchange buffer: an attribute byte with bit 0 set for a directory, then the
 * name with its NUL. DISK_ST_ENDDIR once the directory is walked out, and the
 * handle is closed as that status is answered -- a listing ends on it, so
 * waiting for the window close to collect the handle would leave it open across
 * everything the BASIC program does next.
 *
 * The long name when it is at most DISK_DIR_NAME_MAX characters, the 8.3 alias
 * when it is longer: the rule msx/loader.asm's browser applies to the same
 * reply from CMD_DIR_NEXT (AcceptEntry, NAME_MAX). The driver prints a name in
 * a field exactly that wide and FatFs opens long names as they are, so what
 * this sends is always shown whole and can always be typed back into CALL
 * MOUNT; the alias is only for a name the field could not show whole. A name
 * off a PC can be 255 characters, which is why there has to be a rule at all.
 * When FatFs has no alias to give -- altname is empty on an exFAT volume --
 * the long name goes out whatever its length and the driver cuts it to the
 * field, which is what the emulator's listing does for every entry (it has no
 * alias either; see emul/README.md).
 *
 * Hidden and system entries are skipped, which is what keeps a volume that has
 * been near a Mac from listing a screenful of "._" files. An entry that is
 * neither a directory nor a name ending in ".dsk" is skipped the same way --
 * CALL MOUNT never opens anything else, so a ROM, loader.ini or disk.rom
 * sitting on the volume would only be noise in this listing.
 *
 * Answered with /WAIT released, like the open: a step walks directory sectors,
 * and the skipping above -- hidden and system entries, and now anything CALL
 * MOUNT could not open -- means one command can walk several of them. Nothing
 * here bounds that walk -- a directory whose entries are nearly all hidden, or
 * nearly all something other than a subdirectory or a .dsk image, makes one
 * step read all of them before it answers. What bounds it is the driver's own
 * poll: MailCmd gives up after MAILCMD_PASSES passes and answers ST_TIMEOUT,
 * so the statement prints CARTRIDGE NOT RESPONDING and hands BASIC back
 * rather than leaving a machine that never returns. The line is already
 * released by then, so the Z80 is running its own poll in MSX RAM throughout
 * and MSX DRAM keeps being refreshed.
 *
 * WHY THE OVERLAY MAY BE OPEN WHILE THE BLOB RUNS. This opens the exchange
 * buffer for reading, once per entry, and the statement handler goes on
 * executing out of the BASIC window across all of it -- so for most of a
 * listing the firmware is answering the blob's own instruction fetches from the
 * overlay branch rather than the window branch of the read path. That is safe
 * because the two branches are the same expression: Execute()'s buffer test
 * answers buf[addr - DISK_BUF_START] whether return_buffer or _basicWindow put
 * it there, and the blob was copied into _buf at exactly that offset by the
 * constructor. An open overlay is therefore byte-for-byte unobservable to the
 * code being served. Two changes would break it -- serving the blob from
 * anywhere but _buf, and letting sector data reach DISK_BASIC_START, which
 * DISK_MAX_SECTORS is what prevents.
 */
void CDiskCartridge::CmdBasicDirNext()
{
    // A step with no open behind it can only come from a driver that is not
    // ours: ours sends CMD_BASIC_DIR_OPEN first and stops at DISK_ST_ENDDIR.
    // Refused rather than answered as an empty directory, which would read as
    // a volume with nothing on it.
    if (!_dirOpen) {
        last_cmd_result = 0xFE;
        return;
    }

    releaseWait();

    // On this core's stack rather than in this object: it is nearly 300 bytes
    // with long names enabled, and nothing needs it once the name is copied
    // out. The DIR handle beside it is the only thing that has to outlive the
    // command.
    FILINFO info;
    for (;;) {
        if (f_readdir(&_dir, &info) != FR_OK) {
            CloseBasicDir();
            last_cmd_result = 0xFE;
            return;
        }
        if (!info.fname[0]) {
            CloseBasicDir();
            last_cmd_result = DISK_ST_ENDDIR;
            return;
        }
        if (info.fattrib & (AM_HID | AM_SYS))
            continue;
        if (info.fattrib & AM_DIR)
            break;
        // CALL MOUNT never opens anything but a directory or a .dsk image
        // (CmdBasicMount -> CDiskImage::Mount), so a ROM, loader.ini or
        // disk.rom sitting in the same folder is skipped here just like a
        // hidden or system entry -- it is noise for this statement. fname is
        // FatFs's long name, or the short one for an 8.3-only entry, so the
        // suffix compare has to be case-insensitive either way.
        const char* ext = strrchr(info.fname, '.');
        if (ext && strcasecmp(ext, ".dsk") == 0)
            break;
    }

    _buf[0] = (uint8_t)((info.fattrib & AM_DIR) ? 1 : 0);

    // The rule above. Measured against the bound rather than with strlen:
    // fname is up to FF_LFN_BUF bytes and only whether it runs past
    // DISK_DIR_NAME_MAX decides anything.
    uint32_t len = 0;
    while (len <= DISK_DIR_NAME_MAX && info.fname[len])
        ++len;
    const char* name = (len > DISK_DIR_NAME_MAX && info.altname[0])
                           ? info.altname : info.fname;

    // Bounded by the same count the mount accepts, and terminated whatever
    // happens: the driver walks this string to its NUL without a bound of its
    // own, which is only honest because this side fixes the length. See
    // BasPutz in msx/diskstub.asm.
    uint32_t at = 1;
    for (uint32_t i = 0; i < DISK_BASIC_PATH_MAX && name[i]; ++i)
        _buf[at++] = (uint8_t)name[i];
    _buf[at] = 0;

    return_buffer = true;
    last_cmd_result = 0;
}

// Paths occupy two fixed 65-byte slots; ONEW's size in KiB follows at 130.
// None of these commands hold /WAIT over FatFs or flash writes. The client
// executes its poll from MSX RAM and asks for one sector per STEP.
void CDiskCartridge::CmdFile(uint8_t command)
{
    char first[DISK_PATH_MAX], second[DISK_PATH_MAX];
    memcpy(first, _buf, sizeof first);
    memcpy(second, _buf + DISK_PATH_MAX, sizeof second);
    const uint16_t size = _buf[130] | (uint16_t(_buf[131]) << 8);
    releaseWait();
    if (command != 0x43 && (!memchr(first, 0, sizeof first) ||
        (command == 0x41 && !memchr(second, 0, sizeof second)))) {
        last_cmd_result = DFILE_INVALID;
        return;
    }
    switch (command) {
    case 0x40: last_cmd_result = _files.Delete(first); break;
    case 0x41: last_cmd_result = _files.Copy(first, second); break;
    case 0x42: last_cmd_result = _files.Create(first, size); break;
    case 0x43: last_cmd_result = _files.Step(_buf); break;
    }
}
