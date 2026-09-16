/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Pure logic on a 16 KB Disk ROM image. Deliberately free of Pico SDK and
 * FatFs dependencies so it can be compiled and tested on a host.
 */

#include "RomPatcher.hxx"

// Absolute MSX address of each jump table slot, indexed by DiskBiosEntry.
static const uint16_t kEntryAddr[DBE_COUNT] = {
    0x4010, 0x4013, 0x4016, 0x4019, 0x401C, 0x401F
};

static inline uint16_t rd16(const uint8_t* rom, uint16_t msxAddr)
{
    const uint32_t off = (uint32_t)msxAddr - DISKROM_BASE;
    return (uint16_t)(rom[off] | ((uint16_t)rom[off + 1] << 8));
}

// Opcodes that carry a 16-bit absolute operand in the two bytes that follow.
// The FDC registers are reached with "ld a,(nn)" and "ld (nn),a" in both dumps,
// but the pointer loads are counted too: a driver that sets HL to its status
// register and reads through it refers to the block just as much.
static bool absOperand(uint8_t op)
{
    switch (op) {
    case 0x3A: case 0x32:                       // ld a,(nn) / ld (nn),a
    case 0x21: case 0x11: case 0x01:            // ld hl/de/bc,nn
    case 0x2A: case 0x22:                       // ld hl,(nn) / ld (nn),hl
        return true;
    default:
        return false;
    }
}

uint16_t CRomPatcher::DetectFdcWindow(const uint8_t* rom, uint32_t size)
{
    if (size != DISKROM_SIZE)
        return 0;

    // One counter per DISK_FDC_LEN-byte block of the last page of the image.
    // Scanning every byte offset over-approximates -- an operand-looking pair
    // inside some other instruction counts too -- but the signal is not
    // marginal: in the two dumps measured, the real block draws 40-odd
    // references and every other block in the page draws none.
    const uint16_t regionStart = 0x7F00;
    const uint32_t blocks = (0x8000u - regionStart) / DISK_FDC_LEN;
    uint16_t count[(0x8000u - 0x7F00u) / DISK_FDC_LEN] = { 0 };

    for (uint32_t i = 0; i + 2 < size; ++i) {
        if (!absOperand(rom[i]))
            continue;
        const uint32_t target = (uint32_t)rom[i + 1] | ((uint32_t)rom[i + 2] << 8);
        if (target < regionStart || target >= 0x8000u)
            continue;
        const uint32_t b = (target - regionStart) / DISK_FDC_LEN;
        if (count[b] < 0xFFFFu)
            count[b]++;
    }

    uint32_t best = 0;
    uint16_t bestCount = 0;
    uint16_t runnerUp = 0;
    for (uint32_t b = 0; b < blocks; ++b) {
        if (count[b] > bestCount) {
            runnerUp = bestCount;
            bestCount = count[b];
            best = b;
        } else if (count[b] > runnerUp) {
            runnerUp = count[b];
        }
    }

    // Require a clear winner. A driver that really memory-maps its controller
    // hammers those few addresses; a couple of stray matches in data are not
    // evidence of anything, and guessing wrong is worse than not filling at
    // all -- the window masks whatever the dump keeps there.
    if (bestCount < 8 || bestCount < runnerUp * 2)
        return 0;

    return (uint16_t)(regionStart + best * DISK_FDC_LEN);
}

RomPatchResult CRomPatcher::Validate(const uint8_t* rom, uint32_t size,
                                     RomPatchInfo& info)
{
    if (size != DISKROM_SIZE)
        return RP_BAD_SIZE;

    if (rom[0] != 'A' || rom[1] != 'B')
        return RP_BAD_HEADER;

    info.originalInit = rd16(rom, DISKROM_BASE + 2);

    for (int i = 0; i < DBE_COUNT; ++i) {
        const uint16_t at = kEntryAddr[i];
        const uint32_t off = (uint32_t)at - DISKROM_BASE;

        // MTOFF is the only optional entry, and the standard says an empty
        // slot is signalled by a zero byte rather than by a stub.
        if (i == DBE_MTOFF && rom[off] == 0x00) {
            info.entryPresent[i] = 0;
            continue;
        }

        // Everything else must be JP nn landing inside this ROM. A vendor that
        // put something else there is a vendor whose driver we cannot replace
        // by rewriting three bytes, so refuse rather than guess.
        if (rom[off] != 0xC3)
            return RP_BAD_ENTRY;

        const uint16_t target = rd16(rom, (uint16_t)(at + 1));
        if (target < DISKROM_BASE || target >= DISKROM_BASE + DISKROM_SIZE)
            return RP_BAD_ENTRY;

        info.entryPresent[i] = 1;
    }

    info.fdcAddr = DetectFdcWindow(rom, size);

    return RP_OK;
}

static inline void wr16(uint8_t* rom, uint16_t msxAddr, uint16_t value)
{
    const uint32_t off = (uint32_t)msxAddr - DISKROM_BASE;
    rom[off] = (uint8_t)(value & 0xFF);
    rom[off + 1] = (uint8_t)(value >> 8);
}

void CRomPatcher::Relocate(uint8_t* blob, uint16_t len, const uint8_t* rel,
                           uint16_t base)
{
    const uint16_t delta = (uint16_t)(base - STUB_ASM_ORIGIN);
    const uint16_t count = (uint16_t)(rel[0] | ((uint16_t)rel[1] << 8));

    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t off =
            (uint16_t)(rel[2 + 2 * i] | ((uint16_t)rel[3 + 2 * i] << 8));

        // A malformed table must not walk off the blob.
        if ((uint32_t)off + 1 >= len)
            continue;

        uint16_t w = (uint16_t)(blob[off] | ((uint16_t)blob[off + 1] << 8));
        w = (uint16_t)(w + delta);
        blob[off] = (uint8_t)(w & 0xFF);
        blob[off + 1] = (uint8_t)(w >> 8);
    }
}

RomPatchResult CRomPatcher::Apply(uint8_t* rom, uint8_t* displaced,
                                  uint32_t displacedSize, const uint8_t* stub,
                                  uint16_t stubLen, const uint8_t* rel,
                                  RomPatchInfo& info)
{
    // The window holds the ROM part and nothing else. The only way this fails
    // is a stub grown large enough to run off the end of the 16 KB image,
    // which no dump can cause -- but it is a compile-time constant against a
    // compile-time constant base, so check it rather than assume it.
    const uint32_t len = stubLen;
    if (DISKROM_BORROW_ADDR + len > DISKROM_BASE + DISKROM_SIZE)
        return RP_STUB_TOO_BIG;

    // The window also has to fit in the buffer its displaced bytes go to.
    // Checking it here rather than at the call site keeps the bound with the
    // loop that depends on it.
    if (len > displacedSize)
        return RP_STUB_TOO_BIG;

    info.borrowAddr = (uint16_t)DISKROM_BORROW_ADDR;
    info.borrowLen = (uint16_t)len;

    // Take the vendor's bytes out before writing over them. CMD_DISK_UNBORROW
    // puts exactly these back, so the served image ends up byte-identical to
    // the dump apart from the jump table thunks and the INIT vector.
    uint8_t* dst = &rom[info.borrowAddr - DISKROM_BASE];
    for (uint32_t i = 0; i < len; ++i)
        displaced[i] = dst[i];

    for (uint16_t i = 0; i < stubLen; ++i)
        dst[i] = stub[i];

    Relocate(dst, stubLen, rel, info.borrowAddr);

    for (int i = 0; i < DBE_COUNT; ++i) {
        // GETDPB is deliberately left as the vendor wrote it: it is
        // table-driven off the media descriptor, does no disk I/O, and is
        // already proven on real hardware. A thunkOperand of 0 marks an entry
        // this patcher does not own.
        if (!info.entryPresent[i] || i == DBE_GETDPB) {
            info.thunkOperand[i] = 0;
            continue;
        }
        const uint16_t at = kEntryAddr[i];
        rom[at - DISKROM_BASE] = 0xC3;          // JP nn
        info.thunkOperand[i] = (uint16_t)(at + 1);
        wr16(rom, info.thunkOperand[i], 0);     // filled by SetDispatcher
    }

    // 0x7FFF is the status register as well as the image's last byte, and it
    // answers with this ROM byte whenever no command is in flight. The MSX
    // side polls "not 0xFF" for busy, so the idle answer has to be 0xFF. Both
    // dumps seen so far already hold it, which is exactly why this is written
    // down rather than left to luck.
    rom[DISKROM_SIZE - 1] = 0xFF;

    // Redirect INIT to the prologue. info.originalInit was captured by
    // Validate() and is what the INIT tail calls.
    wr16(rom, DISKROM_BASE + 2, info.borrowAddr);

    // OrigInitAddr is the blob's last two bytes, behind a 5A A5 guard that
    // build_stub.py checks on every build -- so this write cannot silently
    // drift onto code if the stub grows a tail. It is not a relocation site:
    // the value comes from here, not from the blob's assembly origin. The
    // prologue copies it into the tail's own cell before jumping, because by
    // the time the tail wants it the window has gone back to the vendor.
    if (stubLen >= 2)
        wr16(rom, (uint16_t)(info.borrowAddr + stubLen - 2), info.originalInit);

    return RP_OK;
}

void CRomPatcher::PatchStatement(uint8_t* rom, uint16_t entry,
                                 RomPatchInfo& info)
{
    // Borrowed, not replaced: capture the vendor's own address before
    // overwriting it, exactly as Apply does for INIT.
    info.originalStatement = rd16(rom, DISKROM_STATEMENT_ADDR);
    wr16(rom, DISKROM_STATEMENT_ADDR, entry);
}

void CRomPatcher::SwapBorrow(uint8_t* rom, uint8_t* shadow,
                             const RomPatchInfo& info)
{
    uint8_t* live = &rom[info.borrowAddr - DISKROM_BASE];
    for (uint16_t i = 0; i < info.borrowLen; ++i) {
        const uint8_t t = live[i];
        live[i] = shadow[i];
        shadow[i] = t;
    }
}

void CRomPatcher::SetDispatcher(uint8_t* rom, const RomPatchInfo& info,
                                uint16_t ramAddr)
{
    for (int i = 0; i < DBE_COUNT; ++i) {
        // Zero means Apply did not claim this entry -- either absent in the
        // dump, or deliberately left to the vendor.
        if (info.thunkOperand[i] == 0)
            continue;
        wr16(rom, info.thunkOperand[i], (uint16_t)(ramAddr + 3 * i));
    }
}
