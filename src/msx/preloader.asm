; -----------------------------------------------------------------------------
; MSX PICO - HARDWARE PRELOADER
;
; This is the first ROM image the Pico serves to the MSX after reset.
; It runs entirely from MSX RAM (SAFERAM) because it may unmap its own ROM
; when asking the Pico to switch cartridges.
;
; Boot flow:
;   1. Copy "loader.rom" into the cartridge command buffer at $6000.
;   2. Copy this small stub to RAM and jump there.
;   3. Send CMD_MAP_FILE ($01) to the Pico.
;   4. On success: wait for the new ROM to settle, then jump through $4002.
;   5. On failure: send CMD_MAP_SCC ($02) and return to the MSX BIOS.
;      Because the SCC cartridge has no ROM header, the BIOS continues booting
;      and ends up in BASIC with SCC I/O still active.
; -----------------------------------------------------------------------------
    DEVICE NOSLOT64K
    ORG     $4000

CART_BUF    EQU     $6000       ; 8 KB exchange buffer (cartridge page 1)
CART_CMD    EQU     $7FFF       ; Command / status register
CMD_MAPFILE EQU     1           ; Mount a ROM file from flash
CMD_MAPSCC  EQU     2           ; Switch to bare SCC cartridge

; Second address of the reply. While a result stands the firmware answers
; READY_SENTINEL here and the status at CART_CMD, and the poll below requires
; both. It has to: the firmware releases /WAIT and serves no bus at all while a
; handler runs, so the bus floats at the last value driven on it -- the command
; byte this stub just wrote. A poll waiting only for "CART_CMD is not $FF" reads
; that back and calls the command finished, which with CMD_MAPFILE = 1 reports a
; mount failure that never happened, and with a floating $00 reports success and
; jumps through a cartridge vector that has not been swapped in yet. Nothing
; drives two addresses to two different values except the firmware.
CART_STATUS EQU     $7FFE
READY_SENTINEL EQU  $A5

SAFERAM     EQU     $C800       ; RAM area for the switch stub

RomHeader:
    db      "AB"
    dw      Init
    dw      0, 0, 0, 0, 0, 0

Init:
    di

    ; 1. Write the filename "loader.rom" into the cartridge buffer at $6000.
    ld      hl, FileName
    ld      de, CART_BUF
    ld      bc, FileNameEnd - FileName
    ldir

    ; 2. Copy the RAM stub to MSX RAM. We cannot execute from cartridge ROM
    ;    after sending a mapper-switch command because the ROM will disappear.
    ld      hl, Stub
    ld      de, SAFERAM
    ld      bc, StubEnd - Stub
    ldir

    ; 3. Continue execution from RAM.
    jp      SAFERAM

FileName:
    db      "loader.rom", 0
FileNameEnd:

; -----------------------------------------------------------------------------
; RAM STUB - Executes in MSX RAM at SAFERAM
; -----------------------------------------------------------------------------
Stub:
    DISP    SAFERAM

    ; Ask the Pico to mount loader.rom.
    ld      a, CMD_MAPFILE
    ld      (CART_CMD), a

.waitReadyStub:
    ; Poll both halves of the reply: the sentinel at CART_STATUS says the
    ; firmware is driving the bus, and CART_CMD then carries the real status.
    ; The firmware marks the command busy before it releases /WAIT, so the
    ; previous reply cannot be mistaken for this one either.
    ld      a, (CART_STATUS)
    cp      READY_SENTINEL
    jr      nz, .waitReadyStub
    ld      a, (CART_CMD)
    cp      READY_SENTINEL
    jr      z, .waitReadyStub

    ; A = 0 means success, anything else is a failure.
    or      a
    jr      nz, .mountFailed

    ; Small delay to let the new cartridge emulation start before the Z80
    ; fetches the next instruction from $4002.
    ld      bc, 500
.delay:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .delay

    ; Jump through the cartridge init vector of the newly mapped ROM.
    ld      hl, ($4002)
    ei
    jp      (hl)

.mountFailed:
    ; loader.rom is missing or corrupted: fall back to a bare SCC cartridge
    ; and return to the MSX BIOS. The BIOS will skip this slot (no ROM header)
    ; and continue booting into BASIC.
    ld      a, CMD_MAPSCC
    ld      (CART_CMD), a

.waitReadyScc:
    ld      a, (CART_STATUS)        ; gated poll, see .waitReadyStub above
    cp      READY_SENTINEL
    jr      nz, .waitReadyScc
    ld      a, (CART_CMD)
    cp      READY_SENTINEL
    jr      z, .waitReadyScc

    ; Allow the Pico time to switch to SCC emulation before returning to BIOS.
    ld      bc, 500
.delayScc:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .delayScc

    ei
    ret

    ENT
StubEnd:

    SIZE    $2000
    SAVEBIN "../generated/preloader.rom", $4000, $2000
