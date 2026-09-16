; -----------------------------------------------------------------------------
; OmniSCC Loader
; Assembler: sjasmplus
;
; This is the Z80 boot menu for the OmniSCC. It is mapped as an 8 KB ROM
; at $4000, reads /loader.ini from the Pico flash filesystem, and displays up
; to ten favourite entries -- ROM files or .dsk disk images -- in a Screen 5
; menu. The user can select an entry with the cursor keys or digit keys, then
; boot it, or press ESC to return to BASIC.
;
; Assembles to loader_fav.rom. It is the alternative to loader.asm, the browser
; over the whole volume; the two are MUTUALLY EXCLUSIVE, so copy this one to the
; cartridge volume AS loader.rom -- the only name the firmware loads.
;
; Required file:
;   font.inc
;
; font.inc must contain exactly 2048 bytes:
;   256 glyphs * 8 bytes per glyph.
; -----------------------------------------------------------------------------

    DEVICE NOSLOT64K
    ORG     $4000

; =============================================================================
; PICO MAILBOX
; =============================================================================
; The Pico firmware exposes a command mailbox in the cartridge address space.
;
;   CART_BUF  ($6000)  8 KB buffer used for filenames and file data.
;   CART_CMD  ($7FFF)  Write a command code, then read status back.
;                      $FF = busy, $00 = success.
;
; The Pico observes writes to CART_CMD and executes the requested action.
; Commands used by the loader are listed below.

CART_BUF        EQU $6000
CART_CMD        EQU $7FFF

; Second address of the reply. While a result stands the firmware answers
; READY_SENTINEL here and the status at CART_CMD; a poll that requires both --
; the sentinel here AND something else there -- cannot be fooled by a bus
; nobody drives, which reads the same value at every address. That matters
; because the bus is undriven for the whole of every handler: the value it
; holds is the command byte the Z80 itself last wrote, and a one-address poll
; reads that back as a status. See LOADER_READY_SENTINEL in include/Loader.hxx.
CART_STATUS     EQU $7FFE
READY_SENTINEL  EQU $A5

CMD_MAP_FILE    EQU $01    ; mount a ROM file; filename must be in CART_BUF
CMD_MAP_SCC     EQU $02    ; switch cartridge to SCC mapper before exiting
CMD_MAP_DISK    EQU $03    ; mount a .dsk image as a disk interface; path in CART_BUF
CMD_OPEN_READ   EQU $10    ; open a file for reading
CMD_OPEN_WRITE  EQU $11    ; open a file for writing (creates/truncates)
CMD_READ_BLOCK  EQU $12    ; read next block into CART_BUF (size in first 2 bytes)
CMD_WRITE_BLOCK EQU $13    ; write block from CART_BUF (size in first 2 bytes)
CMD_CLOSE       EQU $14    ; close the currently open file

; =============================================================================
; BIOS
; =============================================================================

CHGMOD          EQU $005F
CHGCLR          EQU $0062
SNSMAT          EQU $0141
INITXT          EQU $006C
FILVRM          EQU $0056
KILBUF          EQU $0156
GTSTCK          EQU $00D5
CHSNS           EQU $009C    ; Z = buffer empty, NZ = character waiting
CHGET           EQU $009F    ; A = character code from keyboard buffer
CHPUT           EQU $00A2    ; print character in A to the current screen

FORCLR          EQU $F3E9
BAKCLR          EQU $F3EA
BDRCLR          EQU $F3EB
DPPAGE          EQU $FAF5
ACPAGE          EQU $FAF6

; =============================================================================
; VDP
; =============================================================================

VDP_DATA        EQU $98
VDP_CTRL        EQU $99
VDP_INDIRECT    EQU $9B

; =============================================================================
; RAM
; =============================================================================

RAM_INI_BUF     EQU $C000    ; 2048-byte buffer for /loader.ini

FavPointers     EQU $C800    ; 10 records * 4 bytes ($C800..$C827)
MsgDigitRAM     EQU $C830    ; temporary "N: " string buffer

CfgDefault      EQU $C840    ; default highlighted item (0..9)
CfgDefaultPtr   EQU $C841    ; pointer to default= digit in RAM_INI_BUF (0 = not found)
CfgDelay        EQU $C843    ; auto-boot delay, converted to frames
SavedSP         EQU $C845    ; original stack pointer, restored on exit
IniSize         EQU $C847    ; size of loaded INI file in bytes
CfgSccOnExit    EQU $C864    ; non-zero = map SCC cartridge on ESC
SelectedType    EQU $C865    ; kind of the entry about to boot: 0 = ROM, 1 = .dsk image

; loader.ini's diskrom= value: the Disk ROM dump CMD_MAP_DISK should patch,
; empty for the firmware's default. Sent as a second string after the image
; name, so an image can be paired with the dump from the machine it is meant to
; run on -- the National and Yamaha dumps differ in Disk BASIC and in where
; their controller registers live.
; It is a path, not a bare name: the dump may live in a folder, and fifteen
; characters did not hold the name of one of the two dumps this project is
; tested against -- `diskrom=yis805-128r2_disk.rom` was cut to fifteen, the
; mount failed, and saving wrote the truncation back to the ini. The bytes from
; here to MAILRAM are otherwise unused, so the buffer just grew into them.
DiskRomName     EQU $C868    ; DISKROM_NAME_MAX + 1 bytes, ending below C900
DISKROM_NAME_MAX EQU 64

CurY            EQU $C850    ; current list row Y coordinate
TextX           EQU $C851    ; pixel X for text renderer
TextY           EQU $C852    ; pixel Y for text renderer
TextColor       EQU $C853    ; foreground colour index
TextBgColor     EQU $C854    ; background colour index
GlyphBits       EQU $C855    ; current 8-bit glyph line being rendered

SelectedItem    EQU $C860    ; currently highlighted favourite (0..9)
CountdownStop   EQU $C861    ; 1 = auto-boot stopped by user input
ArrowLock       EQU $C862    ; 1 = arrow key held, prevents repeats
FavCount        EQU $C863    ; number of defined favourite entries

SAFERAM         EQU $CA00    ; small RAM area for stubs that outlive ROM unmap

; MailPoll lives here, copied in by Init before the first mailbox command.
;
; It cannot stay in the ROM it is called from. The firmware releases /WAIT and
; serves nothing for the length of a handler, so an instruction fetched from
; this cartridge in that window reads a bus nobody drives; the loop that has to
; survive it is exactly the one waiting for the handler to finish. Everything
; else may stay in ROM, because by the time MailPoll returns the cartridge is
; answering again.
;
; Clear of the variables below 0xC866 and of the SAFERAM stubs at 0xCA00, which
; are copied over each other and carry their own copies of this loop for the
; same reason -- by then the ROM they came from may be gone entirely.
MAILRAM         EQU $C900

; =============================================================================
; ROM HEADER
; =============================================================================

RomHeader:
    db      "AB"
    dw      Init
    dw      0, 0, 0, 0, 0, 0

RomSignature:
    db      "PICOCART"

; =============================================================================
; INITIALIZATION
; =============================================================================

Init:
    di
    ld      (SavedSP), sp

    xor     a
    ld      (FavCount), a

    ld      hl, FavPointers
    ld      de, FavPointers+1
    ld      bc, 39
    ld      (hl), 0
    ldir

    ; Plant the mailbox poll in RAM before anything sends a command: every
    ; command stops the cartridge answering the bus for as long as its handler
    ; runs, so the loop that waits for it cannot be fetched from here. See
    ; MAILRAM and ExecCartCmd.
    ld      hl, MailPollRom
    ld      de, MAILRAM
    ld      bc, MailPollRomEnd - MailPollRom
    ldir

    ; No diskrom= until ParseIni says otherwise: an empty name means the
    ; firmware's own default.
    xor     a
    ld      (DiskRomName), a

    ; 1. Configure Screen 5 through the BIOS.
    xor     a
    ld      (DPPAGE), a
    ld      (ACPAGE), a
    ld      (CfgSccOnExit), a

    ld      a, 5
    call    CHGMOD

    ld      a, 15
    ld      (FORCLR), a
    ld      a, 4
    ld      (BAKCLR), a
    ld      (BDRCLR), a
    call    CHGCLR

    di

    ; 2. Load /loader.ini and parse it.
    xor     a
    ld      (CfgDefault), a
    ld      (CfgDefaultPtr), a
    ld      (CfgDefaultPtr+1), a

    ld      hl, 500
    ld      (CfgDelay), hl

    call    LoadIniFile

    ld      a, b
    or      c
    jr      z, .skipParse

    xor     a
    ld      (FavCount), a

    ld      hl, RAM_INI_BUF
    call    ParseIni

.skipParse:
    ; Convert the delay from milliseconds to VBlank frames (~50 Hz).
    ; The parser stores milliseconds; the menu loop counts frames.
    ld      hl, (CfgDelay)
    ld      bc, 0
.div20:
    ld      de, 20
    or      a
    sbc     hl, de
    jr      c, .divDone
    inc     bc
    jr      .div20
.divDone:
    ld      (CfgDelay), bc          ; e.g. 3000 ms -> 150 frames (3 s)
    ld      a, (CfgDefault)
    ld      (SelectedItem), a
    xor     a
    ld      (CountdownStop), a
    ld      (ArrowLock), a

    xor     a
    ld      (DPPAGE), a
    ld      (ACPAGE), a    

    ld      a, 5
    call    CHGMOD

    ld      a, 15
    ld      (FORCLR), a
    ld      a, 4
    ld      (BAKCLR), a
    ld      (BDRCLR), a
    call    CHGCLR

    di

    ; Draw the window background and border using VDP commands.
    ld      hl, CmdShadow
    call    DrawBoxVDP

    ld      hl, CmdBorder
    call    DrawBoxVDP

    ld      hl, CmdFill
    call    DrawBoxVDP

    call    WaitVDP

    ; Header title: white on blue.
    ld      a, 15
    ld      (TextColor), a
    ld      a, 4
    ld      (TextBgColor), a

    ld      hl, MsgTitle
    ld      d, 64
    ld      e, 20
    call    DrawString8x8

    ; Draw the favourite list for the first time.
    call    DrawFavList

    ; Footer hint: light blue on blue.
    ld      a, 10
    ld      (TextColor), a
    ld      a, 4
    ld      (TextBgColor), a

    ld      hl, MsgInfo
    ld      d, 16
    ld      e, 172                  ; just below the window
    call    DrawString8x8

    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a

    ei

    ; Wait a few frames so the display settles before polling.
    ld      b, 5
.wait_start:
    halt
    djnz    .wait_start

    ld      de, (CfgDelay)

; =============================================================================
; MAIN MENU LOOP
; =============================================================================

; =============================================================================
; MAIN MENU LOOP
; =============================================================================

PollLoop:
    halt                            ; sync to one VBlank frame (~1/50 s)

    ; If the user has already interrupted the countdown, just poll keys.
    ld      a, (CountdownStop)
    or      a
    jr      nz, .checkKeyBuffer

    ; Otherwise count down. When the frame counter reaches zero, auto-boot.
    ld      de, (CfgDelay)
    ld      a, d
    or      e
    jr      z, .timeExpired

    dec     de
    ld      (CfgDelay), de
    ld      a, d
    or      e
    jr      nz, .checkKeyBuffer

.timeExpired:
    ; Timeout: boot the configured default item.
    ld      a, (CfgDefault)
    jp      BootCartridgeItem

.checkKeyBuffer:
    ; Poll the keyboard buffer through the BIOS.
    call    CHSNS
    jr      z, PollLoop             ; nothing pressed -> next frame

    ; Any keypress permanently cancels the auto-boot countdown.
    ld      a, 1
    ld      (CountdownStop), a

    ; Read and dispatch the pressed key.
    call    CHGET

    cp      $1B
    jp      z, ExitToBasic          ; ESC -> return to BASIC

    cp      13
    jp      z, BootSelectedItem     ; ENTER -> boot highlighted item

    cp      30
    jr      z, .keyUp               ; cursor UP

    cp      31
    jr      z, .keyDown             ; cursor DOWN

    ; Digits (and shifted digit symbols) select a slot directly.
    call    ConvertCharToDigit      ; success: A = 0..9, Carry = 0
    jr      nc, .digitSuccess

    jr      PollLoop

.digitSuccess:
    jp      BootCartridgeItemWithHighlight


; -----------------------------------------------------------------------------
; Move selection UP, wrapping around and skipping empty slots.
; -----------------------------------------------------------------------------

.keyUp:
    ld      a, (SelectedItem)
    ld      e, a                    ; remember current slot for redraw
    ld      b, a
.findPrev:
    ld      a, b
    or      a
    jr      nz, .decSlot
    ld      b, 10
.decSlot:
    dec     b

    ld      l, b
    ld      h, 0
    add     hl, hl
    add     hl, hl
    ld      de, FavPointers
    add     hl, de
    ld      a, (hl)
    inc     hl
    or      (hl)
    jr      z, .findPrev            ; empty slot -> keep searching

    ld      a, b                    ; A = new index
    ld      a, (SelectedItem)
    ld      e, a                    ; E = old index
    ld      a, b                    ; A = new index
    jr      .changeSelectionFast

; -----------------------------------------------------------------------------
; Move selection DOWN, wrapping around and skipping empty slots.
; -----------------------------------------------------------------------------

.keyDown:
    ld      a, (SelectedItem)
    ld      e, a                    ; remember current slot for redraw
    ld      b, a
.findNext:
    inc     b
    ld      a, b
    cp      10
    jr      c, .checkSlot
    ld      b, 0
.checkSlot:
    ld      l, b
    ld      h, 0
    add     hl, hl
    add     hl, hl
    ld      de, FavPointers
    add     hl, de
    ld      a, (hl)
    inc     hl
    or      (hl)
    jr      z, .findNext            ; empty slot -> keep searching

    ld      a, b                    ; A = new index
    ld      a, (SelectedItem)
    ld      e, a                    ; E = old index
    ld      a, b                    ; A = new index

; -----------------------------------------------------------------------------
; Redraw only the old and new rows for fast cursor movement.
; In: A = new SelectedItem, E = old SelectedItem
; -----------------------------------------------------------------------------

.changeSelectionFast:
    cp      e
    jp      z, PollLoop             ; same slot -> nothing to do

    ld      (SelectedItem), a

    push    af
    ld      a, e
    call    DrawSingleFavItem       ; remove highlight from old row
    pop     af

    call    DrawSingleFavItem       ; apply highlight to new row

    jp      PollLoop


; =============================================================================
; KEY PROCESSING AND BOOT
; =============================================================================

; -----------------------------------------------------------------------------
; Convert a typed character to a digit 0..9.
; Handles plain ASCII '0'..'9' and common shifted / AZERTY symbols.
; In:  A = character code
; Out: A = 0..9, Carry = 0 (success)
;      Carry = 1 (not a digit)
; -----------------------------------------------------------------------------

ConvertCharToDigit:
    ; Plain ASCII digits first.
    cp      '0'
    jr      c, .checkShiftSymbols
    cp      '9' + 1
    jr      nc, .checkShiftSymbols

    sub     '0'
    or      a                       ; clear Carry (success)
    ret

.checkShiftSymbols:
    ; Search the alternate code table.
    push    hl
    push    bc
    ld      hl, ShiftDigitMap
    ld      b, ShiftDigitMapEnd - ShiftDigitMap

.mapLoop:
    cp      (hl)                    ; character matched?
    jr      z, .foundInMap
    inc     hl
    inc     hl
    djnz    .mapLoop

    pop     bc
    pop     hl
    scf                             ; Carry = 1 (not found)
    ret

.foundInMap:
    inc     hl
    ld      a, (hl)                 ; A = 0..9
    pop     bc
    pop     hl
    or      a                       ; clear Carry (success)
    ret

; -----------------------------------------------------------------------------
; Alternate character-to-digit table for non-QWERTY / shifted keys.
; -----------------------------------------------------------------------------

ShiftDigitMap:
    ; Shifted symbols on QWERTY / International layouts
    db      '!', 1                  ; Shift + 1
    db      '@', 2                  ; Shift + 2
    db      '#', 3                  ; Shift + 3
    db      '$', 4                  ; Shift + 4
    db      '%', 5                  ; Shift + 5
    db      '^', 6                  ; Shift + 6
    db      '&', 7                  ; Shift + 7
    db      '*', 8                  ; Shift + 8
    db      '(', 9                  ; Shift + 9
    db      ')', 0                  ; Shift + 0

    ; Unshifted symbols on AZERTY / French layouts
    db      '&', 1                  ; Key '1'
    db      '"', 3                  ; Key '3'
    db      39,  4                  ; Key '4' (apostrophe)
    db      '(', 5                  ; Key '5'
    db      '-', 6                  ; Key '6'
    db      '_', 8                  ; Key '8'
    db      '=', 0                  ; Key '0'
ShiftDigitMapEnd:

; =============================================================================
; BOOT WITH VISUAL HIGHLIGHT (200 ms)
; In: A = slot number 0..9
; =============================================================================

BootCartridgeItemWithHighlight:
    ld      c, a                    ; C = target slot

    ; Reject empty slots.
    ld      l, c
    ld      h, 0
    add     hl, hl
    add     hl, hl
    ld      de, FavPointers
    add     hl, de

    ld      e, (hl)
    inc     hl
    ld      d, (hl)

    ld      a, d
    or      e
    jp      z, PollLoop             ; empty slot -> ignore keypress

    ; Move highlight to the selected slot.
    ld      a, (SelectedItem)
    ld      e, a                    ; E = previously selected slot
    ld      a, c
    ld      (SelectedItem), a

    push    af
    ld      a, e
    call    DrawSingleFavItem       ; remove highlight from old row
    pop     af
    call    DrawSingleFavItem       ; highlight the pressed item

    ; Short delay so the user sees the selection before the ROM boots.
    ld      b, 11
.wait200ms:
    halt
    djnz    .wait200ms

    ld      a, (SelectedItem)
    jp      BootCartridgeItem


; Normal boot (by Enter or timeout).
BootSelectedItem:
    ld      a, (SelectedItem)
    ; fallthrough to BootCartridgeItem

BootCartridgeItem:
    ld      c, a
    ld      b, 0
    ld      hl, FavPointers

    ld      d, 0
    ld      e, c
    add     hl, de
    add     hl, de
    add     hl, de
    add     hl, de

    ld      e, (hl)
    inc     hl
    ld      d, (hl)

    ld      a, d
    or      e
    jp      z, PollLoop             ; empty slot -> ignore

    ; Only rewrite /loader.ini if the selected slot differs from the saved default.
    ld      a, (SelectedItem)
    ld      hl, CfgDefault
    cp      (hl)
    jr      z, .noSave

    push    de
    call    RunSaveIniStub          ; update default= digit in loader.ini from RAM
    pop     de

.noSave:
    jp      FireCartridgeCommand

; -----------------------------------------------------------------------------
; RunSaveIniStub copies SaveIniStubRom to SAFERAM and executes it there.
; -----------------------------------------------------------------------------

RunSaveIniStub:
    ld      hl, SaveIniStubRom
    ld      de, SAFERAM
    ld      bc, SaveIniStubEnd - SaveIniStubRom
    ldir
    jp      SAFERAM

; =============================================================================
; ARROW NAVIGATION
; =============================================================================

ProcessArrows:
    push    bc
    push    de
    push    hl

    xor     a                       ; A = 0: keyboard joystick mode for GTSTCK
    call    GTSTCK                  ; A = 1 (up), 5 (down), 0 (none)
    ld      c, a

    pop     hl
    pop     de
    pop     bc

    ld      a, c
    or      a
    jr      nz, .arrowActive

    ; Joystick released: clear the one-shot lock so it can repeat next time.
    xor     a
    ld      (ArrowLock), a
    ret

.arrowActive:
    ld      a, 1
    ld      (CountdownStop), a

    ld      a, (ArrowLock)
    or      a
    ret     nz                      ; already held -> ignore autorepeat

    ld      a, 1
    ld      (ArrowLock), a          ; lock until release

    ld      a, (SelectedItem)
    ld      b, a                    ; B = current item

    ; Determine the highest valid index.
    ld      a, (FavCount)
    or      a
    jr      nz, .haveCount
    ld      a, 6                    ; fallback if parser set nothing
.haveCount:
    dec     a
    ld      d, a                    ; D = max index

    ld      a, c
    cp      1                       ; UP?
    jr      z, .goUp
    cp      5                       ; DOWN?
    jr      z, .goDown
    ret

.goDown:
    ld      a, b
    cp      d
    jr      nc, .downWrap
    inc     a
    jr      .applyNew
.downWrap:
    xor     a
    jr      .applyNew

.goUp:
    ld      a, b
    or      a
    jr      z, .upWrap
    dec     a
    jr      .applyNew
.upWrap:
    ld      a, d

.applyNew:
    ld      (SelectedItem), a

    push    bc
    push    de
    push    hl
    call    DrawFavList             ; redraw the whole list
    pop     hl
    pop     de
    pop     bc
    ret


; =============================================================================
; FAVOURITES DRAWING
; =============================================================================

; -----------------------------------------------------------------------------
; Draw one menu item, applying highlight if it is the selected slot.
; In: A = slot number (0..9)
; -----------------------------------------------------------------------------

DrawSingleFavItem:
    push    af
    push    bc
    push    de
    push    hl
    push    ix

    di

    ld      c, a                    ; C = target slot

    ; Walk the FavPointers table to find the Y coordinate of this slot.
    ; Empty slots are skipped, so rows are packed with 12 px spacing.
    ld      ix, FavPointers
    ld      b, 0
    ld      a, 40
    ld      (CurY), a

.findCoordLoop:
    ld      a, b
    cp      c
    jr      z, .foundSlotCoord

    ld      l, (ix+0)
    ld      h, (ix+1)
    ld      a, h
    or      l
    jr      z, .skipSlot

    ld      a, (CurY)
    add     a, 12                   ; next occupied row is 12 px lower
    ld      (CurY), a

.skipSlot:
    inc     ix
    inc     ix
    inc     ix
    inc     ix
    inc     b
    jr      .findCoordLoop

.foundSlotCoord:
    ld      l, (ix+0)
    ld      h, (ix+1)
    ld      a, h
    or      l
    jr      z, .itemDone            ; empty slot -> nothing to draw

    ; Build "N: " prefix.
    ld      a, c
    add     a, '0'
    ld      (MsgDigitRAM+0), a
    ld      a, ':'
    ld      (MsgDigitRAM+1), a
    ld      a, ' '
    ld      (MsgDigitRAM+2), a
    xor     a
    ld      (MsgDigitRAM+3), a

    ; Colours: selected = blue text on white, normal = white text on blue.
    ld      a, (SelectedItem)
    cp      c
    jr      z, .isSel

    ld      a, 15
    ld      (TextColor), a
    ld      a, 4
    ld      (TextBgColor), a
    jr      .doDraw

.isSel:
    ld      a, 4
    ld      (TextColor), a
    ld      a, 15
    ld      (TextBgColor), a

.doDraw:
    ld      hl, MsgDigitRAM
    ld      d, 16
    ld      a, (CurY)
    ld      e, a
    ld      b, 3
    call    DrawStringPadded

    ; Draw the label if present, otherwise the raw filename.
    ld      l, (ix+2)
    ld      h, (ix+3)
    ld      a, h
    or      l
    jr      nz, .haveLabel

    ld      l, (ix+0)
    ld      h, (ix+1)

.haveLabel:
    ld      d, 40
    ld      a, (CurY)
    ld      e, a
    ld      b, 23
    call    DrawStringPadded

.itemDone:
    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a

    ei

    pop     ix
    pop     hl
    pop     de
    pop     bc
    pop     af
    ret

; -----------------------------------------------------------------------------
; Draw all ten possible slots on startup.
; -----------------------------------------------------------------------------

DrawFavList:
    di

    ld      ix, FavPointers
    ld      b, 0                    ; B = slot 0..9

    ld      a, 40                   ; first occupied row starts at Y = 40
    ld      (CurY), a

.loop:
    ld      l, (ix+0)
    ld      h, (ix+1)
    ld      a, h
    or      l
    jr      z, .next                ; empty slot -> skip

    ld      a, b
    add     a, '0'
    ld      (MsgDigitRAM+0), a
    ld      a, ':'
    ld      (MsgDigitRAM+1), a
    ld      a, ' '
    ld      (MsgDigitRAM+2), a
    xor     a
    ld      (MsgDigitRAM+3), a

    ld      a, (SelectedItem)
    cp      b
    jr      z, .isSel

    ld      a, 15
    ld      (TextColor), a
    ld      a, 4
    ld      (TextBgColor), a
    jr      .doDraw

.isSel:
    ld      a, 4
    ld      (TextColor), a
    ld      a, 15
    ld      (TextBgColor), a

.doDraw:
    push    bc
    push    ix

    ld      hl, MsgDigitRAM
    ld      d, 16
    ld      a, (CurY)
    ld      e, a
    ld      b, 3
    call    DrawStringPadded

    ld      l, (ix+2)
    ld      h, (ix+3)
    ld      a, h
    or      l
    jr      nz, .haveLabel

    ld      l, (ix+0)
    ld      h, (ix+1)

.haveLabel:
    ld      d, 40
    ld      a, (CurY)
    ld      e, a
    ld      b, 23
    call    DrawStringPadded

    pop     ix
    pop     bc

    ld      a, (CurY)
    add     a, 12
    ld      (CurY), a

.next:
    inc     ix
    inc     ix
    inc     ix
    inc     ix

    inc     b
    ld      a, b
    cp      10
    jr      nz, .loop

    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a

    ei
    ret

; =============================================================================
; BOOT STATE RESTORE
; =============================================================================
; RestoreBootScreen puts the VDP and VRAM back into the state the BIOS leaves
; them in before it calls a cartridge init vector.
;
; Booting a game from the menu never resets the machine: the stub below jumps
; straight to $4002 with whatever the loader last drew still on screen. A game
; initialises only the VRAM it actually uses, so leftovers from the Screen 5
; menu survive in every area it never writes. They show up as garbage character
; cells -- usually the last rows of the name table -- and they appear to animate
; with the action, because the character codes left sitting there point at
; pattern entries the game is busy updating.
;
; Screen 1 first, so the VDP is back in a 16K addressing mode and its registers
; and tables are reset, then wipe all of VRAM so nothing of the menu is left.
;
; Interrupts stay off for both, and that is what stops the striped screen that
; used to appear at the handover. CHGMOD leaves SCREEN 1 with the cursor enabled
; and the BIOS interrupt handler then blinks it -- a VRAM write with its own
; address setup -- while FILVRM sets one address and relies on auto-increment
; for 16 KB. A blink landing inside that fill sends the rest of it wherever the
; handler left the address counter: a band cleared, a band not, a band
; overwritten. They are not re-enabled, because the next thing that happens is
; the cartridge being swapped out from under this code.

RestoreBootScreen:
    di

    ld      a, 1
    call    CHGMOD

    xor     a
    ld      hl, $0000
    ld      bc, $4000
    call    FILVRM

    ret

; =============================================================================
; CARTRIDGE MOUNT & SWITCH
; =============================================================================
; FireCartridgeCommand copies a tiny stub to SAFERAM and executes it there.
; The stub sends CMD_MAP_FILE (for a .rom entry) or CMD_MAP_DISK (for a .dsk
; entry) with the filename already placed in CART_BUF, waits for the Pico to
; complete the mount, then either jumps straight to the new cartridge's init
; vector at $4002 (ROM) or soft-resets so the BIOS finds the disk interface
; through its normal slot scan (.dsk).
;
; This must run from RAM, not ROM: once the mount succeeds, the Pico remaps
; $4000-$5FFF to the selected image, so the loader ROM code would vanish
; before it could finish execution.

; -----------------------------------------------------------------------------
; IsDskName returns Z set when the name at HL ends in ".DSK", case-
; insensitively.
;
; The name ends at any of NUL, ',', CR or LF. That set must stay identical to
; the one .copy_name below stops at: the caller passes the raw record out of
; the INI buffer, where the line reads "fav0=file.dsk" and is followed by CR,
; LF and the rest of the file. Miss one terminator and this scans on through
; the whole buffer and compares the last four bytes of something else -- which
; is not a hypothetical, it is what an earlier version of this routine did,
; while the copy below still produced a perfectly clean filename.
;
; It has to be the record in RAM and not the copy in CART_BUF: reads from
; 0x6000-0x7FFE only return buffer contents while a command has the overlay
; open, and none has here -- see the firmware's CLoader::Execute. Classifying
; the copy read back whatever the closed window happens to answer with, which
; is how every .dsk ended up booting as a ROM.
;
; HL and BC are preserved; AF and DE are clobbered.
; -----------------------------------------------------------------------------
IsDskName:
    push    hl
    push    bc
    ld      c, 0                    ; C = length
.scanName:
    ld      a, (hl)
    or      a
    jr      z, .atEnd
    cp      ','
    jr      z, .atEnd
    cp      13
    jr      z, .atEnd
    cp      10
    jr      z, .atEnd
    inc     hl
    inc     c
    jr      .scanName
.atEnd:
    ld      a, c
    cp      4
    jr      c, .noMatch             ; shorter than ".DSK" cannot match
    dec     hl
    dec     hl
    dec     hl
    dec     hl                      ; HL -> the last four characters
    ld      b, 4
    ld      de, StrExtDsk
.cmpChar:
    ld      a, (hl)
    call    UpperCase
    ex      de, hl
    cp      (hl)
    ex      de, hl
    jr      nz, .noMatch
    inc     hl
    inc     de
    djnz    .cmpChar

    pop     bc
    pop     hl
    xor     a                       ; Z set: match
    ret

.noMatch:
    pop     bc
    pop     hl
    or      $FF                     ; Z clear: no match
    ret

; UpperCase folds a lowercase ASCII letter in A to upper case.
UpperCase:
    cp      'a'
    ret     c
    cp      'z' + 1
    ret     nc
    sub     $20
    ret

StrExtDsk:
    db      ".DSK"

FireCartridgeCommand:
    push    de
    call    RestoreBootScreen
    pop     de

    di

    push    de

    ld      hl, SwitchStubRom
    ld      de, SAFERAM
    ld      bc, SwitchStubEnd-SwitchStubRom
    ldir                            ; copy switch stub to RAM

    pop     hl                      ; HL -> filename from FavPointers

    ; Decide which mailbox command SwitchStubRom should send while the name is
    ; still readable. Once it is in CART_BUF it is write-only to us.
    push    hl
    call    IsDskName               ; Z set -> name ends in ".DSK"
    ld      a, 0
    jr      nz, .gotType
    inc     a                       ; A = 1: disk image entry
.gotType:
    ld      (SelectedType), a
    pop     hl

    ld      de, CART_BUF

.copy_name:
    ld      a, (hl)

    cp      ','
    jr      z, .name_copied         ; comma separates filename from label

    or      a
    jr      z, .name_copied         ; NUL terminator

    cp      13
    jr      z, .name_copied         ; CR

    cp      10
    jr      z, .name_copied         ; LF

    ld      (de), a
    inc     hl
    inc     de
    jr      .copy_name

.name_copied:
    xor     a
    ld      (de), a                 ; terminate filename in CART_BUF
    inc     de

    ; Append the Disk ROM dump's name as a second string. Always, not only for
    ; a .dsk entry: an empty first byte means "use the firmware's default", and
    ; CMD_MAP_FILE stops at the image name's terminator and never looks here.
    ld      hl, DiskRomName
.copy_diskrom:
    ld      a, (hl)
    ld      (de), a
    or      a
    jr      z, .diskrom_copied
    inc     hl
    inc     de
    jr      .copy_diskrom

.diskrom_copied:
    jp      SAFERAM                 ; execute the stub from RAM

SwitchStubRom:
    DISP    SAFERAM

    ld      a, (SelectedType)
    or      a
    jr      nz, .diskPath

    ld      a, CMD_MAP_FILE
    ld      (CART_CMD), a

.waitReadyStub:
    ; Gated on both halves of the reply: the sentinel here and a status that is
    ; not the sentinel there. While the handler runs nothing drives the bus, and
    ; it holds the command byte this stub just wrote -- which a one-address poll
    ; read back as a status, reporting a mount failure that never happened or,
    ; worse, a success that let the jump below run before the cartridge had been
    ; swapped.
    ld      a, (CART_STATUS)
    cp      READY_SENTINEL
    jr      nz, .waitReadyStub
    ld      a, (CART_CMD)
    cp      READY_SENTINEL
    jr      z, .waitReadyStub

    or      a
    jr      nz, .mountFailed        ; non-zero status = failure

    ld      bc, 500
.delay:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .delay              ; short settle delay

    ld      hl, ($4002)             ; new cartridge init vector
    ei
    jp      (hl)

    ; -------------------------------------------------------------------------
    ; Disk image: CMD_MAP_DISK loads and patches disk.rom, opens the .dsk
    ; image, and swaps the cartridge to a disk interface, all in one mailbox
    ; round trip -- exactly like CMD_MAP_FILE above for a plain ROM. The new
    ; cartridge cannot simply be jumped into, though: the MSX BIOS only calls
    ; a Disk ROM's INIT during its power-on slot scan, so a soft reset is
    ; required to make the BIOS look again and find it.
    ; -------------------------------------------------------------------------
.diskPath:
    ld      a, CMD_MAP_DISK
    ld      (CART_CMD), a

.waitDiskReady:
    ld      a, (CART_STATUS)        ; gated poll, see .waitReadyStub above
    cp      READY_SENTINEL
    jr      nz, .waitDiskReady
    ld      a, (CART_CMD)
    cp      READY_SENTINEL
    jr      z, .waitDiskReady

    or      a
    jr      nz, .diskMountFailed    ; non-zero status = failure

    ld      bc, 500
.diskDelay:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .diskDelay          ; short settle delay

    di
    jp      0                       ; soft reset: BIOS rescans slots for the disk ROM

.mountFailed:
    di
    rst     $00                     ; reset on mount failure

    ; -------------------------------------------------------------------------
    ; A failed .dsk mount is the likely first-run outcome: the disk path
    ; depends on a second file, disk.rom, that the menu never lists, so
    ; "forgot to copy it" is the most probable cause. Resetting blind like
    ; .mountFailed above would present that as an unexplained reset loop with
    ; a half-second window to escape it. Show why instead, and wait for the
    ; user to acknowledge it before going back to the menu. The ROM path
    ; above keeps its existing reset-on-failure behaviour unchanged.
    ; -------------------------------------------------------------------------
.diskMountFailed:
    ei                              ; CHSNS/CHGET need the keyboard scan running

    ; RestoreBootScreen (called before this stub even starts, see
    ; FireCartridgeCommand) switched to SCREEN 1 and then FILVRM'd
    ; 0x0000-0x3FFF -- which wipes the pattern generator table CHGMOD had
    ; just filled, along with the colour table. Printing straight onto that
    ; would draw blank glyphs in colour 0 on 0: invisible. INITXT
    ; re-establishes SCREEN 0 with the BIOS's own default character set
    ; before anything is drawn.
    call    INITXT

    ; A keystroke still sitting in the BIOS type-ahead buffer -- e.g. the
    ; ENTER that selected this entry in the first place -- would otherwise
    ; satisfy CHSNS immediately below and dismiss the message before the
    ; user ever sees it.
    call    KILBUF

    ld      hl, DiskFailMsg
.printFail:
    ld      a, (hl)
    or      a
    jr      z, .waitFailKey
    push    hl
    call    CHPUT
    pop     hl
    inc     hl
    jr      .printFail

.waitFailKey:
    call    CHSNS
    jr      z, .waitFailKey         ; nothing pressed yet
    call    CHGET                   ; consume it

    jp      Init                    ; back to a freshly redrawn menu

DiskFailMsg:
    db      13, 10
    db      "DISK MOUNT FAILED", 13, 10
    db      "CHECK disk.rom EXISTS", 13, 10
    db      "PRESS ANY KEY...", 0

    ENT
SwitchStubEnd:

; -----------------------------------------------------------------------------
; SaveIniStubRom is copied to SAFERAM and executed there. It updates the
; default= digit in RAM_INI_BUF and rewrites /loader.ini through the Pico mailbox.
; Running from RAM lets the Z80 spin on CART_CMD while the Pico is busy with
; flash erase/program cycles, without needing the loader ROM to stay responsive.
; -----------------------------------------------------------------------------

SaveIniStubRom:
    DISP    SAFERAM

    ; Patch the default= digit in the RAM copy of /loader.ini.
    ld      hl, (CfgDefaultPtr)
    ld      a, h
    or      l
    jr      z, .openFile            ; no default= line -> just rewrite file

    ld      a, (SelectedItem)
    add     a, '0'
    ld      (hl), a

.openFile:
    ; Copy "/loader.ini", 0 to CART_BUF.
    ld      hl, StrIniFile
    ld      de, CART_BUF
.copyName:
    ld      a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    or      a
    jr      nz, .copyName

    ld      a, CMD_OPEN_WRITE
    call    .execCmd
    or      a
    ret     nz                      ; open failed; boot anyway

    ; Build the write block: 16-bit length at CART_BUF[0..1], payload at [2..].
    ld      a, (IniSize)
    ld      c, a
    ld      a, (IniSize+1)
    ld      b, a
    ld      a, c
    ld      (CART_BUF), a
    ld      a, b
    ld      (CART_BUF+1), a

    ld      hl, RAM_INI_BUF
    ld      de, CART_BUF+2
    ldir

    ld      a, CMD_WRITE_BLOCK
    call    .execCmd

    ld      a, CMD_CLOSE
    call    .execCmd
    ret

.execCmd:
    ld      (CART_CMD), a
.waitReady:
    ; Gated poll: the sentinel at CART_STATUS and a different value at CART_CMD.
    ; This stub writes the file, so its handler erases and programs flash and the
    ; bus stays undriven for tens of milliseconds -- the longest window in the
    ; protocol, and the one where an ungated poll would take the floating command
    ; byte for a status and run the next command out of order.
    ld      a, (CART_STATUS)
    cp      READY_SENTINEL
    jr      nz, .waitReady
    ld      a, (CART_CMD)
    cp      READY_SENTINEL
    jr      z, .waitReady
    ret

    ENT
SaveIniStubEnd:

    INCLUDE "mailbox.inc"

; =============================================================================
; EXIT TO BASIC
; =============================================================================
; ExitToBasic restores the text screen and, if requested, leaves the cartridge
; mapped as an SCC sound cartridge.
;
; The SCC switch is performed by a tiny stub copied to SAFERAM. It must run
; from RAM because after CMD_MAP_SCC the loader ROM at $4000 is no longer
; mapped; executing ROM code would crash as soon as the Pico swaps banks.

ExitToBasic:
    ld      a, 15
    ld      (FORCLR), a
    ld      a, 4
    ld      (BAKCLR), a
    ld      (BDRCLR), a

    call    CHGCLR
    call    INITXT
    call    KILBUF

    ld      a, (CfgSccOnExit)
    or      a
    jr      z, .noScc

    ld      hl, SccExitStubRom
    ld      de, SAFERAM
    ld      bc, SccExitStubEnd - SccExitStubRom
    ldir                            ; copy SCC stub to RAM

    ld      sp, (SavedSP)
    jp      SAFERAM                 ; finish exit from RAM

.noScc:
    ld      sp, (SavedSP)
    ei
    ret

SccExitStubRom:
    DISP    SAFERAM

    ld      a, CMD_MAP_SCC
    ld      (CART_CMD), a

    ld      bc, 100
.delayScc:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .delayScc

    ld      sp, (SavedSP)
    ei
    ret

    ENT
SccExitStubEnd:

    INCLUDE "vdp.inc"

; =============================================================================
; INI PARSER
; =============================================================================
; ParseIni scans the loaded /loader.ini buffer line by line.
;
; Supported keys:
;   favN=<filename>[,<label>]   Assign a ROM or .dsk file to slot N (0..9).
;                               A name ending in ".DSK" (case-insensitive) is
;                               mounted as a disk image; anything else boots
;                               as a ROM. The optional label is shown in the
;                               menu.
;   default=<0..9>              Set the initially highlighted slot.
;   delay=<milliseconds>        Set the auto-boot countdown length.
;   diskrom=<filename>          Disk ROM dump to patch for .dsk entries,
;                               instead of the firmware's default disk.rom.
;                               A path is allowed, so the dump can live in a
;                               folder. At most DISKROM_NAME_MAX characters,
;                               and no spaces -- the value ends at the first
;                               one.
;   scc_on_exit=<0..9>          If non-zero, map SCC cartridge on ESC.
;
; Lines beginning with ';', '#' or '[' are ignored, as is whitespace.
; The parser is intentionally simple and case-sensitive.

ParseIni:
.nextLine:
.skipWhitespace:
    ld      a, (hl)
    or      a
    ret     z                       ; end of buffer

    cp      ' '
    jr      z, .nextChar
    cp      9
    jr      z, .nextChar
    cp      13
    jr      z, .nextChar
    cp      10
    jr      nz, .checkWord

.nextChar:
    inc     hl
    jr      .skipWhitespace

.checkWord:
    ld      a, (hl)

    ; Skip section headers and comments.
    cp      '['
    jp      z, .skipToEOL
    cp      ';'
    jp      z, .skipToEOL
    cp      '#'
    jp      z, .skipToEOL

    ; 'd' begins delay= or default=.
    cp      'd'
    jp      z, .checkD

    ; 's' begins scc_on_exit=.
    cp      's'
    jp      z, .checkS

    ; 'f' begins favN=.
    cp      'f'
    jp      nz, .skipToEOL

    ; -------------------------------------------------------------------------
    ; favN=<filename>[,<label>]
    ; -------------------------------------------------------------------------
    inc     hl
    ld      a, (hl)
    cp      'a'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      'v'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    sub     '0'
    cp      10
    jp      nc, .skipToEOL

    ld      c, a                    ; C = slot number 0..9
    inc     hl

.findEqualFav:
    ld      a, (hl)
    or      a
    ret     z
    cp      13
    jp      z, .skipToEOL
    cp      10
    jp      z, .skipToEOL
    cp      '='
    jr      z, .foundEqualFav
    inc     hl
    jr      .findEqualFav

.foundEqualFav:
    inc     hl

.skipSpaceFav:
    ld      a, (hl)
    cp      ' '
    jr      z, .skipSpF
    cp      9
    jr      nz, .gotFilePtr
.skipSpF:
    inc     hl
    jr      .skipSpaceFav

.gotFilePtr:
    ld      d, h
    ld      e, l                    ; DE -> filename

    ; HL = FavPointers + C * 4
    ld      l, c
    ld      h, 0
    add     hl, hl
    add     hl, hl
    ld      bc, FavPointers
    add     hl, bc

    ; Store filename pointer in the first word of the record.
    ld      (hl), e
    inc     hl
    ld      (hl), d
    inc     hl

    push    hl                      ; save address for the label pointer
    ex      de, hl

.scanForComma:
    ld      a, (hl)
    or      a
    jr      z, .endOfLine
    cp      13
    jr      z, .endOfLine
    cp      10
    jr      z, .endOfLine
    cp      ','
    jr      z, .hasComma
    inc     hl
    jr      .scanForComma

.endOfLine:
    pop     de
    xor     a
    ld      (de), a
    inc     de
    ld      (de), a                 ; no label -> store NULL
    jr      .updateFavCount

.hasComma:
    inc     hl

.skipSpaceLabel:
    ld      a, (hl)
    cp      ' '
    jr      z, .skipSpL
    cp      9
    jr      nz, .gotLabelPtr
.skipSpL:
    inc     hl
    jr      .skipSpaceLabel

.gotLabelPtr:
    pop     de
    ld      a, l
    ld      (de), a
    inc     de
    ld      a, h
    ld      (de), a                 ; store label pointer

.updateFavCount:
    ; FavCount is the highest slot index + 1 that has been defined.
    ld      a, (FavCount)
    inc     c
    cp      c
    jr      nc, .noFavInc
    ld      a, c
    ld      (FavCount), a
.noFavInc:
    jp      .skipToEOL

    ; -------------------------------------------------------------------------
    ; delay= / default=
    ; -------------------------------------------------------------------------
.checkD:
    inc     hl
    ld      a, (hl)
    cp      'e'
    jr      z, .checkDe
    cp      'i'
    jr      z, .isDiskRom
    jp      .skipToEOL

.checkDe:
    inc     hl
    ld      a, (hl)
    cp      'l'
    jp      z, .isDelay             ; jp, not jr: the diskrom= block below sits
    cp      'f'                     ; between here and both of these targets
    jp      z, .isDefault
    jp      .skipToEOL

; -----------------------------------------------------------------------------
; diskrom=<filename>
;
; Names the Disk ROM dump CMD_MAP_DISK patches, instead of the firmware's
; default. Only the "di" is matched before the scan for '=' starts, which is how
; every other key here works -- the parser disambiguates rather than validates.
; -----------------------------------------------------------------------------
.isDiskRom:
.findEqRom:
    ld      a, (hl)
    or      a
    ret     z
    cp      13
    jp      z, .skipToEOL
    cp      10
    jp      z, .skipToEOL
    cp      '='
    jr      z, .foundEqRom
    inc     hl
    jr      .findEqRom

.foundEqRom:
    inc     hl

.skipSpRom:
    ld      a, (hl)
    cp      ' '
    jr      z, .nextSpRom
    cp      9
    jr      nz, .copyRomName
.nextSpRom:
    inc     hl
    jr      .skipSpRom

.copyRomName:
    ld      de, DiskRomName
    ld      b, DISKROM_NAME_MAX     ; the buffer is one longer, for the NUL
.copyRomChar:
    ld      a, (hl)
    or      a
    jr      z, .romNameDone
    cp      13
    jr      z, .romNameDone
    cp      10
    jr      z, .romNameDone
    cp      ','
    jr      z, .romNameDone
    cp      ' '
    jr      z, .romNameDone         ; a name stops at whitespace; anything after
                                    ; it on the line is a comment
    ld      (de), a
    inc     hl
    inc     de
    djnz    .copyRomChar

.romNameDone:
    xor     a
    ld      (de), a
    jp      .skipToEOL

.isDefault:
    inc     hl                  ; 'a'
    inc     hl                  ; 'u'
    inc     hl                  ; 'l'
    inc     hl                  ; 't'
    inc     hl

.findEqDef:
    ld      a, (hl)
    or      a
    ret     z
    cp      13
    jp      z, .skipToEOL
    cp      10
    jp      z, .skipToEOL
    cp      '='
    jr      z, .foundEqDef
    inc     hl
    jr      .findEqDef

.foundEqDef:
    inc     hl

.skipSpDef:
    ld      a, (hl)
    cp      ' '
    jr      z, .nextSpDef
    cp      9
    jr      nz, .readDefDigit
.nextSpDef:
    inc     hl
    jr      .skipSpDef

.readDefDigit:
    ld      a, (hl)
    sub     '0'
    cp      10
    jp      nc, .skipToEOL
    ld      (CfgDefault), a
    ld      (CfgDefaultPtr), hl     ; remember where the digit lives in RAM_INI_BUF
    jp      .skipToEOL

.isDelay:
    inc     hl                  ; 'a'
    inc     hl                  ; 'y'
    inc     hl

.findEqDelay:
    ld      a, (hl)
    or      a
    ret     z
    cp      13
    jp      z, .skipToEOL
    cp      10
    jp      z, .skipToEOL
    cp      '='
    jr      z, .parseDelayVal
    inc     hl
    jr      .findEqDelay

.parseDelayVal:
    inc     hl

.skipSpD:
    ld      a, (hl)
    cp      ' '
    jr      z, .skipSpD2
    cp      9
    jr      nz, .readDelayNum
.skipSpD2:
    inc     hl
    jr      .skipSpD

.readDelayNum:
    ld      de, 0

.delayDigit:
    ld      a, (hl)
    sub     '0'
    cp      10
    jr      nc, .delayDone

    push    hl
    ld      l, e
    ld      h, d
    add     hl, hl
    ld      b, h
    ld      c, l
    add     hl, hl
    add     hl, hl
    add     hl, bc
    ld      c, a
    ld      b, 0
    add     hl, bc
    ex      de, hl
    pop     hl

    inc     hl
    jr      .delayDigit

.delayDone:
    ld      a, d
    or      e
    jp      z, .skipToEOL           ; ignore zero delay

    ld      (CfgDelay), de
    jp      .skipToEOL              ; delay= is done; do not fall into scc_on_exit=

    ; -------------------------------------------------------------------------
    ; scc_on_exit=
    ; -------------------------------------------------------------------------
.checkS:
    inc     hl
    ld      a, (hl)
    cp      'c'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      'c'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      '_'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      'o'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      'n'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      '_'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      'e'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      'x'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      'i'
    jp      nz, .skipToEOL

    inc     hl
    ld      a, (hl)
    cp      't'
    jp      nz, .skipToEOL

    inc     hl

.findEqScc:
    ld      a, (hl)
    or      a
    ret     z
    cp      13
    jp      z, .skipToEOL
    cp      10
    jp      z, .skipToEOL
    cp      '='
    jr      z, .foundEqScc
    inc     hl
    jr      .findEqScc

.foundEqScc:
    inc     hl

.skipSpScc:
    ld      a, (hl)
    cp      ' '
    jr      z, .nextSpScc
    cp      9
    jr      nz, .readSccDigit
.nextSpScc:
    inc     hl
    jr      .skipSpScc

.readSccDigit:
    ld      a, (hl)
    sub     '0'
    cp      10
    jp      nc, .skipToEOL
    ld      (CfgSccOnExit), a
    jp      .skipToEOL

.skipToEOL:
    ld      a, (hl)
    or      a
    ret     z
    cp      13
    jr      z, .eolFound
    cp      10
    jr      z, .eolFound
    inc     hl
    jr      .skipToEOL

.eolFound:
    inc     hl
    jp      .nextLine

; =============================================================================
; VDP COMMAND DATA (window for 10 rows)
; =============================================================================

CmdShadow:
    dw      10, 32, 236, 134        ; shadow (X, Y, W, H)
    db      1, 0, $C0

CmdBorder:
    dw      6, 30, 244, 134         ; white border
    db      15, 0, $C0

CmdFill:
    dw      8, 32, 240, 130         ; blue window background
    db      4, 0, $C0

; =============================================================================
; KEYBOARD TABLE
; =============================================================================

KeyTable:
    db      0,   1,   '0'
    db      0,   2,   '1'
    db      0,   4,   '2'
    db      0,   8,   '3'
    db      0,  16,   '4'
    db      0,  32,   '5'
    db      0,  64,   '6'
    db      0, 128,   '7'
    db      1,   1,   '8'
    db      1,   2,   '9'

; =============================================================================
; TEXT AND FONT
; =============================================================================

StrIniFile:
    db      "/loader.ini", 0
StrIniFileEnd:

MsgTitle:
    db      "MSX PICO BOOT", 0

MsgInfo:
    db      "UP/DN=SEL ENT=BOOT ESC", 0

Font8x8:
    INCLUDE "font.inc"
Font8x8End:

    ASSERT Font8x8End-Font8x8 = 2048

; =============================================================================
; ROM OUTPUT
; =============================================================================

    ASSERT  $ <= $6000
    SIZE    $2000
    SAVEBIN "../generated/loader_fav.rom", $4000, $2000
