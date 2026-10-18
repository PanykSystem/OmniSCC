; -----------------------------------------------------------------------------
; MSX PICO - HARDWARE PRELOADER
;
; This is the first ROM image the Pico serves to the MSX after reset.
; It runs entirely from MSX RAM (SAFERAM) because it may unmap its own ROM
; when asking the Pico to switch cartridges.
;
; Boot flow:
;   1. Copy this small stub to RAM and jump there.
;   2. Scan the volume root for the first regular file matching loader*.rom
;      (case-insensitive, directory order), preserving its full filename.
;   3. Send CMD_MAP_FILE ($01); filename tags select custom ROM mappers.
;   4. On success: wait for the new ROM to settle, then jump through $4002.
;   5. If no file matches or mounting fails: try /nextor.rom and reset.
;   6. If Nextor fails too: send CMD_MAP_SCC ($02) and return to the MSX BIOS.
; -----------------------------------------------------------------------------
    DEVICE NOSLOT64K
    ORG     $4000

CART_BUF    EQU     $6000       ; 8 KB exchange buffer (cartridge page 1)
CART_CMD    EQU     $7FFF       ; Command / status register
CMD_MAPFILE EQU     1           ; Mount a ROM file from flash
CMD_MAPNEXTOR EQU   4           ; Mount the shared-volume Nextor interface
CMD_CLOSEBUFFER EQU 0
CMD_DIROPEN EQU    $20
CMD_DIRNEXT EQU    $21
CMD_DIRCLOSE EQU   $23
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

    ; Copy the stub before any command: directory I/O also stops ROM serving.
    ld      hl, Stub
    ld      de, SAFERAM
    ld      bc, StubEnd - Stub
    ldir

    ; Continue execution from RAM.
    jp      SAFERAM

; -----------------------------------------------------------------------------
; RAM STUB - Executes in MSX RAM at SAFERAM
; -----------------------------------------------------------------------------
Stub:
    DISP    SAFERAM

    ; Open the root, then walk visible entries in the firmware's order.
    ld      a, "/"
    ld      (CART_BUF), a
    xor     a
    ld      (CART_BUF+1), a
    ld      a, CMD_DIROPEN
    call    .command
    or      a
    jp      nz, .mountFailed
.scan:
    ld      a, CMD_DIRNEXT
    call    .command
    or      a
    jr      nz, .notFound
    ld      a, (CART_BUF)
    or      a
    jr      nz, .skipEntry       ; Directories are never loader candidates.

    ; Keep the complete long name in RAM before closing the reply buffer.
    ; FatFs names contain at most 255 bytes plus their terminator.
    ld      hl, CART_BUF+1
    ld      de, .filename
.copyName:
    ld      a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    or      a
    jr      nz, .copyName
    xor     a                   ; CMD_CLOSE_BUFFER
    call    .command

    ld      hl, .filename
    ld      de, .prefix
    ld      b, 6
.checkPrefix:
    ld      a, (hl)
    or      $20
    ld      c, a
    ld      a, (de)
    cp      c
    jr      nz, .scan
    inc     hl
    inc     de
    djnz    .checkPrefix

    ; Find the terminator, then compare the final four bytes with ".rom".
.findEnd:
    ld      a, (hl)
    inc     hl
    or      a
    jr      nz, .findEnd
    ld      de, -5
    add     hl, de
    ld      de, .extension
    ld      b, 4
.checkExtension:
    ld      a, (hl)
    or      $20
    ld      c, a
    ld      a, (de)
    cp      c
    jr      nz, .scan
    inc     hl
    inc     de
    djnz    .checkExtension

    ld      a, CMD_DIRCLOSE
    call    .command
    ld      hl, .filename
    ld      de, CART_BUF
.writeName:
    ld      a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    or      a
    jr      nz, .writeName
    ld      a, CMD_MAPFILE
    call    .command
    or      a
    jp      nz, .mountFailed
    jr      .mounted
.skipEntry:
    xor     a                   ; Close each directory reply before continuing.
    call    .command
    jr      .scan
.notFound:
    ld      a, CMD_DIRCLOSE
    call    .command
    jp      .mountFailed
.prefix:
    db      "loader"
.extension:
    db      ".rom"
.mounted:
    ; Jump through the successfully mapped loader ROM.
    ld      bc, 500
.delay:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .delay
    ld      hl, ($4002)
    ei
    jp      (hl)

.command:
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

    ret                         ; A = completed command status

.mountFailed:
    ; No usable menu: try the shared-volume disk interface from RAM.
    ld      hl, .nextorName
    ld      de, CART_BUF
    ld      bc, .nextorNameEnd-.nextorName
    ldir
    ld      a, CMD_MAPNEXTOR
    ld      (CART_CMD), a
    call    .waitReadyStub
    or      a
    jr      nz, .nextorFailed
    ; The BIOS must rediscover the new disk cartridge through its slot scan.
    jp      0
.nextorName:
    db      "/nextor.rom",0
.nextorNameEnd:
.nextorFailed:
    ; Nextor is unavailable too: fall back to a bare SCC cartridge
    ; and return to the MSX BIOS. The BIOS will skip this slot (no ROM header)
    ; and continue booting into BASIC.
    ld      a, CMD_MAPSCC
    ld      (CART_CMD), a
    call    .waitReadyStub

    ; Allow the Pico time to switch to SCC emulation before returning to BIOS.
    ld      bc, 500
.delayScc:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .delayScc

    ei
    ret

    ; Scratch storage is copied with the stub and survives cartridge commands.
.filename:
    defs    256, 0

    ENT
StubEnd:

    SIZE    $2000
    SAVEBIN "../generated/preloader.rom", $4000, $2000
