/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 */

#pragma once

#include <stdint.h>

// A Disk ROM is always exactly 16 KB and always lives at 0x4000 on the MSX.
#define DISKROM_SIZE 16384u
#define DISKROM_BASE 0x4000u

// The origin diskstub.asm is assembled at. Relocation is always relative to it.
#define STUB_ASM_ORIGIN 0x8000u

// Where the stub blobs go in the patched image.
//
// Real Disk ROMs have no free space. Both dumps this was first tried against
// -- a National and a Yamaha YIS805, 80% identical Disk BASIC over different
// FDC drivers -- have at most 195 and 26 contiguous filler bytes, against the
// ~200 the ROM part needs, and the bodies of the routines we replace turn out
// to be full of entry points the rest of the ROM calls (LD HL,74FD at 4849,
// JP P 75FD at 4846, and so on in both dumps). There is nothing to take.
//
// So the space is borrowed, not taken. Between the BIOS reading the "AB"
// header and the INIT tail calling the vendor INIT, the only code executing
// from page 1 is the prologue itself -- so during that window any address
// outside the header and jump table is ours. CMD_DISK_UNBORROW, issued by the
// INIT tail from MSX RAM, puts the vendor's bytes back before a single vendor
// instruction runs. That ordering is not a nicety: the vendor executes code
// inside this window (a boot trace shows 408F..40B7 running as ordinary Disk
// BASIC). The window is re-armed on the BIOS's next INIT-vector fetch; see
// CDiskCartridge::Execute.
//
// 0x4022 is simply the first address past the jump table. Nothing about it is
// special, and nothing needs to be: the bytes there are restored intact.
#define DISKROM_BORROW_ADDR 0x4022u

// The STATEMENT vector: how MSX BASIC finds a cartridge's extended CALL
// statements. Fixed by the MSX standard, same as the jump table addresses.
#define DISKROM_STATEMENT_ADDR 0x4004u

// The Disk BIOS jump table. Addresses are fixed by the MSX standard; every
// disk interface exposes the same six three-byte slots.
enum DiskBiosEntry {
    DBE_DSKIO = 0,   // 0x4010
    DBE_DSKCHG,      // 0x4013
    DBE_GETDPB,      // 0x4016
    DBE_CHOICE,      // 0x4019
    DBE_DSKFMT,      // 0x401C
    DBE_MTOFF,       // 0x401F, present only when the byte there is non-zero
    DBE_COUNT
};

enum RomPatchResult {
    RP_OK = 0,
    RP_BAD_SIZE,        // not exactly 16384 bytes
    RP_BAD_HEADER,      // no "AB" signature
    RP_BAD_ENTRY,       // a jump table slot is not a JP landing inside the ROM
    RP_STUB_TOO_BIG     // the blobs do not fit between the borrow window and
                        // the end of the 16 KB image, or do not fit in the
                        // caller's buffer for the bytes they displace
};

struct RomPatchInfo {
    uint16_t originalInit;              // INIT vector read from the ROM header
    // STATEMENT vector read from the ROM header, before PatchStatement
    // redirected it. Zero when the dump published no extended statements --
    // BasicChain must then refuse the name rather than jump to address 0.
    uint16_t originalStatement;
    uint16_t borrowAddr;                // MSX address of the borrow window
    uint16_t borrowLen;                 // its length in bytes
    uint16_t thunkOperand[DBE_COUNT];   // MSX address of each patched JP operand
    uint8_t  entryPresent[DBE_COUNT];   // 0 when the vendor left the slot empty

    // Base of the vendor's memory-mapped floppy controller, found by
    // DetectFdcWindow. Every MSX disk interface maps its FDC into the top of
    // page 1 and the vendor INIT probes it, but not at the same address: the
    // National dump uses 0x7FB8, the Yamaha YIS805 uses 0x7F80. Hard-coding one
    // of them leaves the other interface's registers answering with ROM bytes,
    // and its writes landing in the exchange buffer instead of going nowhere.
    // Zero when no candidate stood out, which turns the fill off.
    uint16_t fdcAddr;
};

// Bytes of the controller block, from fdcAddr. Eight covers a WD279x's four
// registers and the mirror most interfaces decode above them.
#define DISK_FDC_LEN 8u

// One step of the borrow window's re-arming sniff, over one cartridge bus
// cycle. `state` is opaque and starts at 0. Returns true on the cycle that
// completes reads of 0x4000, 0x4001, 0x4002, 0x4003 in that order with no
// other bus cycle of any kind in between -- the MSX BIOS slot scan checking
// the "AB" id and then fetching the INIT vector, immediately before it calls
// INIT, on every boot.
//
// Four addresses rather than the two that carry the vector, because the cost
// is nothing and the specificity is not: the measured INIT scan (spec, "##
// Measured") is exactly 0x4000, 0x4001, 0x4002, 0x4003 and then the INIT
// entry, and the other reader of cartridge headers, BASIC's CALL dispatch,
// never touches these four at all -- it reads only 0x4004/0x4005 (see
// RomStatementScanStep). Any write, and any read anywhere else -- the status
// register, the exchange overlay, the SCC windows -- resets the sequence, so
// "immediately" means what it says.
//
// Inline because CDiskCartridge::Execute calls it on every bus cycle and that
// loop must not reach into flash. See that call site for what happens if this
// ever does fire spuriously.
static inline bool RomHeaderScanStep(uint8_t& state, uint16_t addr,
                                     bool isWrite)
{
    if (isWrite) {
        state = 0;
        return false;
    }
    if (addr == (uint16_t)(DISKROM_BASE + state)) {
        if (++state == 4) {
            state = 0;
            return true;
        }
        return false;
    }
    state = (addr == (uint16_t)DISKROM_BASE) ? 1 : 0;
    return false;
}

// A read of 0x4004 followed immediately by a read of 0x4005, with no other
// cartridge bus cycle in between: BASIC's CALL dispatch reading this
// cartridge's STATEMENT vector, low byte then high byte, immediately before
// entering it with CALSLT.
//
// MEASURED, in openMSX, on both supported dumps (spec, "## Measured"). The
// device's header-read trace for CALL FOO at the Ok prompt is, on the
// National dump and byte for byte the same on the Yamaha:
//
//     hdr read 4004
//     hdr read 4005
//       then 6576        <- the vendor's STATEMENT, the vector not yet
//       then 6577           redirected in that build; 7800 once it is
//
// No 0x4000/0x4001 before it. BASIC does not walk the headers for CALL: the
// BIOS records at boot, in SLTATR (0xFCC9, bit 5), which slots publish a
// STATEMENT handler, and the dispatch walks that table and reads only the
// vector. The earlier four-read guess (id bytes, then the vector) never
// occurs at dispatch time, and a sniff waiting for it would never fire.
//
// The same pair also fires once at boot, harmlessly. Filling SLTATR, the BIOS
// reads 0x4004, 0x4005, 0x4006, 0x4007 ... (measured: "hdr read 4004",
// "hdr read 4005", "then 4006"), so this returns true on the 0x4005 read
// there too. That is fine because the sniff decides nothing on its own: the
// arm it sets in CDiskCartridge::Execute lives exactly one cycle and opens the
// window only if that cycle is a read of DISK_BASIC_START. The 0x4006 read is
// not, and drops it with nothing installed.
//
// The sibling of RomHeaderScanStep, and deliberately its own state byte. The
// two sequences no longer share an address, but each zeroes its state on any
// read that is not its own next one, so a shared byte would let the header
// step erase this one's progress (0x4005 is "not 0x4000" to it) before this
// step looked at it -- and the dispatch would never be seen.
static inline bool RomStatementScanStep(uint8_t& state, uint16_t addr,
                                        bool isWrite)
{
    if (isWrite) {
        state = 0;
        return false;
    }
    // 0 -> 0x4004, 1 -> 0x4005
    if (addr == (uint16_t)(DISKROM_STATEMENT_ADDR + state)) {
        if (++state == 2) {
            state = 0;
            return true;
        }
        return false;
    }
    state = (addr == (uint16_t)DISKROM_STATEMENT_ADDR) ? 1 : 0;
    return false;
}

class CRomPatcher {
public:
    // Check size, header and the six jump table slots. Fills `info.originalInit`
    // and `info.entryPresent`; leaves the region and thunk fields untouched.
    // Find the vendor's FDC block: the DISK_FDC_LEN-byte window in the top of
    // page 1 that the image's own code refers to most. Returns 0 when nothing
    // stands out. Called by Validate, which stores it in info.fdcAddr.
    static uint16_t DetectFdcWindow(const uint8_t* rom, uint32_t size);

    static RomPatchResult Validate(const uint8_t* rom, uint32_t size,
                                   RomPatchInfo& info);

    // Apply a relocation table produced by msx/build_stub.py. `rel` is a
    // little-endian uint16 count followed by that many uint16 byte offsets,
    // each naming a word in `blob` that holds an address relative to the
    // assembly origin STUB_ORG. `base` is where the blob will actually live.
    static void Relocate(uint8_t* blob, uint16_t len, const uint8_t* rel,
                         uint16_t base);

    // Open the borrow window and patch the image.
    //
    // The stubLen bytes displaced at DISKROM_BORROW_ADDR are copied out to
    // `displaced` first; `displacedSize` is how much room that buffer has, and
    // a window that would not fit in it is refused with RP_STUB_TOO_BIG rather
    // than overrunning it. It is what CMD_DISK_UNBORROW puts back, and
    // SwapBorrow() is the only thing that should ever touch it afterwards.
    //
    // The window holds the ROM part alone. The RAM dispatcher reaches MSX RAM
    // through the exchange buffer (CmdDiskSetRam), which the INIT tail can
    // safely read because by then it is running from page 3 -- so nothing but
    // the stub needs room here.
    //
    // Then: the relocated stub goes into the window, every present jump table
    // entry except GETDPB becomes a JP 0 placeholder (SetDispatcher fills the
    // operands once the MSX RAM address is known), and the header INIT vector
    // at 0x4002 is redirected to the start of the stub. info.originalInit
    // keeps the vendor's INIT; it is patched into the stub's OrigInitAddr cell
    // -- the blob's last two bytes, behind a guard build_stub.py checks -- and
    // the INIT tail calls it. GETDPB is left as the vendor wrote it -- it is
    // table-driven off the media descriptor and does no disk I/O -- and its
    // thunkOperand reads back 0 to mark it unclaimed.
    static RomPatchResult Apply(uint8_t* rom, uint8_t* displaced,
                                uint32_t displacedSize, const uint8_t* stub,
                                uint16_t stubLen, const uint8_t* rel,
                                RomPatchInfo& info);

    // Redirect the STATEMENT vector at DISKROM_STATEMENT_ADDR to `entry`.
    // Borrowed, not replaced: info.originalStatement keeps the vendor's own
    // address, because CALL FORMAT and CALL SYSTEM still have to reach the
    // vendor's handler -- BasChain in the statement blob chains through it.
    // Separate from Apply because `entry` is a disk-cartridge concept (the
    // BASIC window's address, DISK_BASIC_START, where the bus loop serves
    // the statement blob from), not one this patcher otherwise knows.
    static void PatchStatement(uint8_t* rom, uint16_t entry,
                               RomPatchInfo& info);

    // Exchange the borrow window's contents with `shadow`, which therefore
    // always holds whichever version of those bytes is not currently being
    // served -- the vendor's while the stub is installed, the stub's while the
    // vendor's are back. Calling it twice restores the starting state, which
    // is exactly what the arm/unborrow cycle needs.
    static void SwapBorrow(uint8_t* rom, uint8_t* shadow,
                           const RomPatchInfo& info);

    // Point every entry Apply claimed (thunkOperand != 0) at ramAddr + 3 *
    // entry, which is where the dispatcher's own jump table lives once the
    // prologue has copied it into MSX RAM. Called from the CMD_DISK_SETRAM
    // handler.
    static void SetDispatcher(uint8_t* rom, const RomPatchInfo& info,
                              uint16_t ramAddr);
};
