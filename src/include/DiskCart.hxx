/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 */

#pragma once

#include "Cartridge.hxx"
#include "DiskImage.hxx"
#include "DiskFiles.hxx"
#include "RomPatcher.hxx"
#include "SCCSound.hxx"

// The patched Disk ROM occupies the whole of page 1.
#define DISK_ROM_START 0x4000
#define DISK_ROM_END   0x7FFF

// Exchange buffer, overlaid on the ROM window only while a command has opened
// it. Sector data starts at the base so the Z80 side is a plain LDIR.
#define DISK_BUF_START 0x6000
#define DISK_BUF_END   0x7FFE
#define DISK_BUF_SIZE  0x2000

// Parameter block, at the top of the exchange buffer.
#define DISK_P_LBA_LO  0x1E00
#define DISK_P_LBA_MID 0x1E01
#define DISK_P_LBA_HI  0x1E02
#define DISK_P_COUNT   0x1E03
#define DISK_P_DRIVE   0x1E04
#define DISK_P_FLAGS   0x1E05

// Handshake for CMD_DISK_SETRAM. The INIT tail writes these two bytes into the
// parameter block immediately before the command; CmdDiskSetRam refuses a 0x35
// that arrives without them and clears them once used.
//
// The vendor INIT now runs before our allocation, and it writes to what it
// believes are its own FDC registers -- 0x7FFF is one of them on plenty of
// interfaces. A plain one-shot flag would spend itself on such a stray write
// and then refuse the only 0x35 that mattered. See diskstub.asm's
// SETRAM_MAGIC0 comment.
#define DISK_P_MAGIC0  0x1E06
#define DISK_P_MAGIC1  0x1E07

#define DISK_SETRAM_MAGIC0 0xA5
#define DISK_SETRAM_MAGIC1 0x5A

// Longest path CMD_BASIC_MOUNT will accept, matching diskrom='s limit in
// loader.asm -- settled there after a 21-character dump name was silently
// cut to 15. DiskImage.hxx's DISK_PATH_MAX is this plus the NUL terminator;
// test_constants.py checks the two stay in step.
#define DISK_BASIC_PATH_MAX 64

// Longest name CMD_BASIC_DIR_NEXT sends as the entry's long name. An entry
// whose long name is longer goes out as its 8.3 alias instead, so that what
// CALL DIR prints is always the whole of a name that CALL MOUNT will open.
// The shape of the rule is the loader's -- AcceptEntry in loader.asm keeps the
// long name when it fits the browser's row and takes the alias otherwise --
// but the threshold is not: the browser's NAME_MAX is 31, and this is 32
// because that is CALL DIR's field -- six columns of "<DIR> " and 32 of name
// are the 38-column line the two-column listing already fit within, inside
// SCREEN 0's line at its 39-column default as well as at WIDTH 40. The
// driver's field is DIR_NAME_MAX in diskstub.asm, which must be this same
// figure; test_constants.py checks the two stay in step. What reaches this
// limit is already filtered to what CALL MOUNT can open -- a directory or a
// .dsk image -- so trimming to this width never has to consider anything else.
#define DISK_DIR_NAME_MAX 32

// Code window. While a disk call is in flight the firmware answers reads here
// out of the exchange buffer, where the dispatcher blob sits, instead of out of
// the Disk ROM image -- so the driver executes from the cartridge and needs
// almost no MSX RAM. See the WINDOW PART banner in msx/diskstub.asm; WIN_BASE
// and WIN_SIZE there must match, which test_constants.py checks.
//
// It is open only while our own code runs. The vendor bytes underneath are live
// Disk BASIC in both dumps -- an opcode-anchored scan finds 14 references into
// this range in the National image and 24 in the Yamaha one -- so hiding them
// permanently would break Disk BASIC. The trampoline in MSX RAM opens the
// window on the way into a disk call and closes it on the way out, under DI.
//
// These two figures (14/24) name no method and do not reproduce with
// test/host/patchrom.cxx's scan, which gives 6 and 11; see the BASIC window's
// historical design notes for the original 7800h BASIC window.
#define DISK_CODE_START 0x7C00
#define DISK_CODE_END   0x7DFF

// BASIC code window. Helpers for file operations live at 7600h, while the
// published statement entry stays at 7800h for existing DOS clients. Loading
// uses START; PatchStatement and the armed entry fetch use ENTRY.
//
// The window hides vendor ROM only during a statement. Entry neutralises
// H.KEYI/H.TIMI before BIOS calls can enable interrupts; both exits restore
// them and close the entire window from the RAM trampoline. Code, scratch
// and all sector transfers must remain on their respective sides of START.
// No additional MSX RAM is reserved by expanding this window.
#define DISK_BASIC_START 0x7600
#define DISK_BASIC_ENTRY 0x7800
#define DISK_BASIC_END   0x7BFF

// Sectors one command may return. The sector data area is everything from
// DISK_BUF_START up to the LOWER of the two windows, so this cannot be raised
// without moving that one: a read that ran past it would overwrite the code the
// Z80 is executing. BASIC occupies three sectors, leaving eleven for data.
#define DISK_MAX_SECTORS 11

// Command reply. While a result is ready the command register (0x7FFF) answers
// with DISK_READY_TOKEN and the status register (0x7FFE) answers with the
// status; both keep answering until the next command write clears the result,
// and neither read changes anything.
//
// The firmware holds /WAIT asserted through every command handler it can, so
// the Z80 stays stalled on the command write until the result is ready and
// never reads an undriven bus while a command is in flight. The exceptions are
// the two commands that touch flash -- CmdDiskWrite and CmdDiskFlush -- where
// holding it would stall the Z80 for tens of milliseconds and stop it
// refreshing MSX DRAM; those release /WAIT first and are answered while the
// driver polls from a loop in MSX RAM (MailCmd in diskstub.asm).
//
// The MSX-side poll reads both registers and requires them to differ -- the
// token at 0x7FFF and a non-token status at 0x7FFE. An undriven MSX bus floats
// to the same value at every address, so a matching pair can only come from the
// firmware actually driving; that is what makes the released-/WAIT commands
// safe, and it is a cheap guard for the rest. No status this firmware produces
// is 0xA5 -- the set it sends is 0x00/0x02/0x04/0x06/0x08/0x0A/0xFE, and
// test_constants.py resolves every served status and checks it -- which is
// what keeps the guard valid and is the one invariant to preserve here. The
// driver adds one of its own on the MSX side, ST_TIMEOUT (0x0C), for a command
// the firmware never answered; it is not in this list because nothing here
// sends it.
#define DISK_STATUS_REG  0x7FFE
#define DISK_READY_TOKEN 0xA5

// The two statuses the driver maps to specific DSKIO errors rather than to the
// generic failure. Write protection answers code 0 instead of write fault
// (code 10), so a caller knows not to retry; an empty drive answers code 2, not
// ready, which is what drive B is whenever loader.ini has no disk2= line.
// diskstub.asm's ST_WRPROT and ST_NOTREADY must match, which test_constants.py
// checks.
#define DISK_ST_WRPROT   0x0A
#define DISK_ST_NOTREADY 0x02

// The two statuses the BASIC statement commands answer with. A name that is not
// on the volume and a directory walked to its end are both ordinary outcomes a
// BASIC program acts on -- FILES printing nothing, a KILL reporting "File not
// found" -- rather than failures, so they are distinct from the generic 0xFE.
// Neither may equal DISK_READY_TOKEN; test_constants.py resolves every status
// served through a name and checks that.
#define DISK_ST_NOFILE   0x06
#define DISK_ST_ENDDIR   0x08

// Below this every read in our window is either exchange buffer or a plain ROM
// byte, which is what lets the bus loop answer the common case -- an
// instruction fetch, and the Z80 runs all of Disk BASIC out of this ROM -- one
// compare after sampling the address. Everything that answers as something
// else (the command and status registers, and the controller block, which
// CRomPatcher::DetectFdcWindow only ever looks for in 0x7F00-0x7FFF) lives at
// or above it. The SCC windows are higher still, in page 2.
#define DISK_SPECIAL_START 0x7F00

// Floppy controller register fill.
//
// Every MSX disk interface memory-maps its FDC into the top of page 1, and the
// vendor INIT probes it. There is no controller here, so those reads return
// whatever the dump holds -- and for the National image that is 0xFF, which its
// own wait loop reads as "still busy":
//
//   77B8  ld a,(7FB8)   ; status register
//   77BB  rra           ; carry = bit 0, BUSY
//   77BC  jr c,77B8     ; spin
//
// It hangs there forever. Filling the window with 0x00 answers "idle, ready"
// instead: for a WD279x type-I status bit 0 is BUSY, bit 2 is TRACK0 and bit 7
// is NOT READY. Settled for the National image in the emulator: with the fill
// its poll ends after six reads and Disk BASIC boots; without it the poll never
// ends. 0x04 and 0x24 were indistinguishable from 0x00 there, so the fill is
// what matters and the value is not.
//
// WHERE the window sits is not a constant, because it is not the same on every
// interface: the National dump maps its controller at 0x7FB8, the Yamaha YIS805
// at 0x7F80. It is detected per image by CRomPatcher::DetectFdcWindow and kept
// in RomPatchInfo::fdcAddr, DISK_FDC_LEN bytes long; zero means nothing stood
// out and the fill is off. Hard-coding one interface's address left the other
// one's registers answering with ROM bytes and its writes landing in the
// exchange buffer -- which is exactly what a session with the Yamaha image
// measured, nine writes into 0x7F80 and 0x7F84.
#define DISK_FDC_FILL_ENABLED 1
#define DISK_FDC_FILL         0x00

/**
 * Disk interface cartridge.
 *
 * Serves the patched 16 KB Disk ROM, the mailbox overlay and the SCC windows
 * from one bus loop. The MSX-side driver lives in MSX RAM and polls 0x7FFF,
 * because the firmware stops answering the bus while a handler runs.
 */
class CDiskCartridge : public CCartridge {
public:
    // `image` is drive A and `image2` is drive B. The second one need not be
    // mounted: an unmounted drive answers DISK_ST_NOTREADY, which the driver
    // turns into DSKIO's "not ready", and that is what the MSX sees for B:
    // when loader.ini names no disk2=.
    CDiskCartridge(COmniSCC& multiCart, CSCCSound& scc, uint8_t* patchedRom,
                   uint8_t* exchangeBuf, uint8_t* borrowShadow,
                   CDiskImage& image, CDiskImage& image2, CDiskFiles& files,
                   const RomPatchInfo& info);
    virtual ~CDiskCartridge();

    bool Execute() override;

private:
    uint8_t* _rom;                  // 16 KB patched image, RAM-resident

    // DISK_BUF_SIZE bytes of exchange window. Owned by COmniSCC and carved out
    // of its cartridge staging buffer, because a dedicated 8 KB array does not
    // fit in what is left of the RP2040's SRAM.
    uint8_t* _buf;

    // The other half of the borrow window: whichever version of those bytes is
    // not currently being served. CRomPatcher::Apply left the vendor's bytes
    // here; every swap exchanges them with what the ROM buffer holds. Also
    // owned by COmniSCC, and at least _info.borrowLen bytes.
    uint8_t* _shadow;

    // True while the stub occupies the borrow window, i.e. between arming it
    // and CMD_DISK_UNBORROW. Guards both directions against a double swap.
    bool _borrowActive;

    CDiskImage& _image;             // drive A
    CDiskImage& _image2;            // drive B, possibly not mounted
    RomPatchInfo _info;
    CSCCSound& _scc;

    bool return_buffer;

    // True while the code window answers with the dispatcher blob rather than
    // the ROM image, i.e. between CMD_DISK_WINDOW_ON and CMD_DISK_WINDOW_OFF.
    // Independent of return_buffer: the dispatcher keeps executing across the
    // commands that open and close the sector overlay.
    bool _driverWindow;

    // True while the BASIC window answers with the statement handler rather
    // than the ROM image. Opened by the bus loop itself, on the armed fetch of
    // DISK_BASIC_ENTRY and on nothing else -- BASIC reaches the handler through
    // CALSLT, not through a command write, so there is no way to ask for it
    // first -- and closed by CMD_BASIC_WINDOW_OFF when the handler is done.
    bool _basicWindow;

    uint8_t last_cmd_result;

    // CALL DIR's directory, open between CMD_BASIC_DIR_OPEN and the step that
    // walks off the end of it. It has to outlive a command because a listing is
    // one round trip per entry, and it is the only state in this class that
    // does -- everything else a command needs is in the exchange buffer when
    // the command arrives.
    DIR _dir;

    // Whether that handle means anything. It is what a step without an open
    // behind it is refused on, and what the window's reset drops a listing on:
    // a statement abandoned by an MSX reset never sends the window close that
    // would have ended it.
    bool _dirOpen;

    // The drive the parameter block names, mounted or not, or null when there
    // is no such drive -- which is every drive past B.
    CDiskImage* DriveSlot();

    // The same by number, for the one command that is about both drives rather
    // than about the one the parameter block names. One copy of the drive ->
    // image mapping, because two would be a mapping that can drift.
    CDiskImage* DriveSlot(uint8_t drive);

    // The image the drive byte in the parameter block names, or null when that
     // drive has no disk -- which includes every drive past B.
    CDiskImage* DriveImage();

    void CmdDiskRead();
    void CmdDiskWrite();
    void CmdDiskFlush();
    void CmdDiskFormat();
    void CmdDiskStatus();
    void CmdDiskSetRam();
    void CmdDiskUnborrow();
    CDiskFiles& _files;
    void CmdFile(uint8_t command);
    void CmdBasicMount();
    void CmdBasicEject();
    void CmdBasicInfo();
    void CmdBasicDirOpen();
    void CmdBasicDirNext();

    // Close the listing's directory, if one is open. Cheap enough to call on
    // every statement's window close, which is what makes it the one place a
    // listing that stopped early is cleaned up from.
    void CloseBasicDir();
};
