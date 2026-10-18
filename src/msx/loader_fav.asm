; -----------------------------------------------------------------------------
; OmniSCC Loader
; Assembler: sjasmplus
;
; This is the Z80 boot menu for the OmniSCC. It is mapped as a 16 KB ROM
; at $4000, reads /loader_fav.ini from the Pico flash filesystem, and displays up
; to ten favourite entries -- ROM files or .dsk disk images -- in a Screen 5
; or MSX1 SCREEN 1 menu. The user can select an entry with the cursor keys or digit keys, then
; boot it, or press ESC to return to BASIC.
;
; Assembles to loader_fav.rom. It is the alternative to loader.asm, the browser
; over the whole volume; the two are MUTUALLY EXCLUSIVE, so copy this one to the
; cartridge volume as the only loader*.rom. The OmniSCC header selects its mapper.
;
; Required file:
;   font.inc
;
; font.inc must contain exactly 2048 bytes:
;   256 glyphs * 8 bytes per glyph.
; -----------------------------------------------------------------------------

    DEFINE ROM_DATABASE_MENU
    DEFINE FAV_TEXT
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

RAM_INI_BUF     EQU $C000    ; 2048-byte buffer for /loader_fav.ini

FavPointers     EQU $C800    ; 10 records * 4 bytes ($C800..$C827)
LastSecs        EQU $C998    ; last painted seconds, FF = badge absent
CountStr        EQU $C999    ; two decimal digits and NUL
COUNT_X         EQU 226
COUNT_Y         EQU 2

MsgDigitRAM     EQU $C830    ; temporary "N: " string buffer

CfgDefault      EQU $C840    ; default highlighted item (0..9)
CfgDefaultPtr   EQU $C841    ; pointer to default= digit in RAM_INI_BUF (0 = not found)
CfgDelay        EQU $C843    ; auto-boot delay, converted to frames
SavedSP         EQU $C845    ; original stack pointer, restored on exit
IniSize         EQU $C847    ; size of loaded INI file in bytes
VolumeFrames    EQU $C9A2
VolumeDirty     EQU $C9A3
RbscProgress    EQU $C9A4    ; next letter of the hidden keyboard sequence
VolumeText      EQU $C9B0    ; 23 bytes for popup/Help, below SAFERAM
VOLUME_BROWSER EQU 0
CfgSccVolume    EQU $C9A1
CfgMultidisk    EQU $C9A0
CfgSccOnExit    EQU $C864    ; non-zero = map SCC cartridge on ESC
SelectedType    EQU $C865    ; kind of the entry about to boot: 0 = ROM, 1 = .dsk image

; loader_fav.ini's diskrom= value: the Disk ROM dump CMD_MAP_DISK should patch,
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
JoystickState   EQU $C976    ; five bytes, shared joystick polling state
HelpActive      EQU $C97B    ; modal help is visible
HelpF1Held      EQU $C97C    ; one toggle per physical F1 press
CfgColorRom     EQU $C97D    ; decimal MSX palette index, 0..15
MountPath       EQU $C97F    ; pointer to the chosen filename in RAM_INI_BUF
FwHelpText      EQU $C981    ; 16 bytes, below SAFERAM
CfgColorDsk     EQU $C97E
FavCount        EQU $C863    ; number of defined favourite entries

; Fast text scratch and four palettes, clear of the ini, mailbox and exit stubs.
CfgKucOptions   EQU $CB81
RomDatabasePath EQU $CB40    ; 65 bytes, before glyph tables at CC00
FavBandText     EQU $CB00    ; 23 padded characters
FavBandTextPtr  EQU $CB28
FavHiliteCmd    EQU $CB30    ; 11-byte LMMV command
FavBandCount    EQU $CB20
FavBandLine     EQU $CB21
FavBandAddr     EQU $CB22
FavBandTable    EQU $CB24
FavBuildFg      EQU $CB26
FavBuildBg      EQU $CB27
FavGlyphTable   EQU $CC00    ; 1024 bytes: ROM colour on blue
FavGlyphSel     EQU $D000    ; 1024 bytes: blue on ROM colour
FavGlyphDsk     EQU $D400    ; 1024 bytes: DSK colour on blue
FavGlyphDskSel  EQU $D800    ; 1024 bytes: blue on DSK colour, ends at DC00

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
NextorRomName   EQU $C930
NextorInvalid   EQU $C972
NextorLastError EQU $C973
BootFailed      EQU $C975
BOOT_FAILED_MARK EQU $A5
NEXTOR_BROWSER EQU 0

COLOR_TEXT      EQU 15
COLOR_FOOTER    EQU 10
FOOTER_X        EQU 40       ; centered Help/Open, matching the browser
FOOTER_Y        EQU 200
COLOR_BG        EQU 4
TITLE_X         EQU 8
TITLE_Y         EQU 2

MAILRAM         EQU $C900

; =============================================================================
; ROM HEADER
; =============================================================================

RomHeader:
    db      "AB"
    dw      Init
    dw      0, 0, 0, 0, 0, 0

RomSignature:
    db      "OmniSCC", 0         ; loader mapper signature at ROM offset 16

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
    ld      (NextorRomName), a
    ld      (NextorInvalid), a
    ld      (IniSize), a
    ld      (IniSize+1), a
    ld      (RAM_INI_BUF), a

    call    DetectUi

    ; 1. Configure the selected video mode through the BIOS.
    xor     a
    ld      (DPPAGE), a
    ld      (ACPAGE), a
    ld      (CfgSccOnExit), a
    call    InitColors

    call    InitAutoScreen

    ld      a, 15
    ld      (FORCLR), a
    ld      a, 4
    ld      (BAKCLR), a
    ld      (BDRCLR), a
    call    CHGCLR

    call    DrawStartupSplash

    di

    ld      a, 1
    ld      (CfgMultidisk), a
    ld      a, 100
    ld      (CfgSccVolume), a
    xor     a
    ld      (VolumeFrames), a
    ld      (VolumeDirty), a

    ; 2. Load /loader_fav.ini and parse it.
    xor     a
    ld      (CfgDefault), a
    ld      (CfgDefaultPtr), a
    ld      (CfgDefaultPtr+1), a

    ld      hl, 500
    ld      (CfgDelay), hl

    call    InitRomDatabase
    call    LoadIniFile

    ld      a, b
    or      c
    jr      z, .skipParse

    xor     a
    ld      (FavCount), a

    ld      hl, RAM_INI_BUF
    call    ParseIni

.skipParse:
    call    ApplyRomDatabase
    call    ApplyKucOptions
    call    ApplyDiskOptions
    call    ApplySccVolume
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
    ld a, b
    or c
    jr nz, .storeFrames
    ld hl, (CfgDelay)
    ld a, h
    or l
    jr z, .storeFrames
    inc bc
.storeFrames:
    ld      (CfgDelay), bc          ; e.g. 3000 ms -> 150 frames (3 s)
    ld      a, (CfgDefault)
    ld      (SelectedItem), a
    xor     a
    ld      (CountdownStop), a
    ld      (ArrowLock), a
    ld      (HelpActive), a
    ld      (HelpF1Held), a
    ld      (RbscProgress), a
    ld      a, (BootFailed)
    cp      BOOT_FAILED_MARK
    call    nz, InitJoystick
    ; A failed mount returns through Init: keep its countdown cancelled.
    ld      a, (BootFailed)
    cp      BOOT_FAILED_MARK
    jr      nz, .autoOk
    xor     a
    ld      (BootFailed), a
    inc     a
    ld      (CountdownStop), a
.autoOk:
    call    DisableZeroDelay
    ld      a, $FF
    ld      (LastSecs), a


    call    PrepareStartupMenu

    ld      a, 15
    ld      (FORCLR), a
    ld      a, 4
    ld      (BAKCLR), a
    ld      (BDRCLR), a
    call    CHGCLR

    di

    call    BuildFavGlyphTables

    call    DrawFrame
    call    DrawFavList
    call    DrawFooter
    call    DrawCountdown
    call    ShowStartupScreen
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
    call    TickVolume
    call    PollHelpKey
    jp      nz, ToggleHelp
    ld      a, (HelpActive)
    or      a
    jr      z, .browser
    call    CHSNS
    jr      nz, .gotKey
    jp      PollLoop
.browser:

    call    PollJoystick
    jr      nz, .gotJoystick
    call    CHSNS
    jr      nz, .gotKey

    call    DrawCountdown

    ; Both input sources can cancel even a zero-delay auto-boot.
    ld      a, (CountdownStop)
    or      a
    jp      nz, PollLoop
    ld      de, (CfgDelay)
    ld      a, d
    or      e
    jr      z, .timeExpired
    dec     de
    ld      (CfgDelay), de
    ld      a, d
    or      e
    jp      nz, PollLoop
.timeExpired:
    ld      a, (CfgDefault)
    jp      BootCartridgeItem
.gotJoystick:
    cp      8
    jp      z, ExitToBasic          ; joystick B -> BASIC
    jr      .gotInput
.gotKey:
    call    CHGET
    push af
    ld a, (HelpActive)
    cp 3
    jr nz, .notRbscLogo
    pop af
    jp CloseHelp                   ; every keyboard character closes the emblem
.notRbscLogo:
    pop af
    call FeedRbscKey
    jp c, OpenRbscLogo

.gotInput:
    ld      b, a
    ld      a, (HelpActive)
    or      a
    ld      a, b
    jr      z, .browserInput
    cp      $1B
    jp      z, CloseHelp
    jp      PollLoop
.browserInput:
    push    af
    ld      a, 1
    ld      (CountdownStop), a
    call    DrawCountdown
    pop     af
    cp      '+'
    jp      z, VolumeUpKey
    cp      '-'
    jp      z, VolumeDownKey
    cp      $1B
    jr      nz, .dismissVolume
    ld      a, (VolumeFrames)
    or      a
    jr      z, .ordinaryEscape
    call    DismissVolume
    jp      PollLoop
.ordinaryEscape:
    ld      a, $1B
.dismissVolume:
    push    af
    call    DismissVolume
    pop     af
    cp      $1B
    jp      z, ExitToBasic          ; ESC -> return to BASIC

    cp      ' '
    jp      z, BootSelectedItem     ; SPACE
    cp      13
    jp      z, BootSelectedItem     ; ENTER -> boot highlighted item

    cp      30
    jr      z, .keyUp               ; cursor UP

    cp      31
    jr      z, .keyDown             ; cursor DOWN

    ; Digits (and shifted digit symbols) select a slot directly.
    call    ConvertCharToDigit      ; success: A = 0..9, Carry = 0
    jr      nc, .digitSuccess

    jp      PollLoop

.digitSuccess:
    jp      BootCartridgeItemWithHighlight


; -----------------------------------------------------------------------------
; Move selection UP, wrapping around and skipping empty slots.
; -----------------------------------------------------------------------------

.keyUp:
    ld      a, (SelectedItem)
    ld      e, a                    ; remember current slot for redraw
    ld      b, a
    ld      c, 10                   ; bound the search when all slots are empty
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
    jr      nz, .prevFound
    dec     c
    jp      z, PollLoop             ; no populated slot
    jr      .findPrev
.prevFound:

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
    ld      c, 10
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
    jr      nz, .nextFound
    dec     c
    jp      z, PollLoop
    jr      .findNext
.nextFound:

    ld      a, b                    ; A = new index
    ld      a, (SelectedItem)
    ld      e, a                    ; E = old index
    ld      a, b                    ; A = new index

; -----------------------------------------------------------------------------
; Toggle only the old and new rows for fast cursor movement.
; In: A = new SelectedItem, E = old SelectedItem
; -----------------------------------------------------------------------------

.changeSelectionFast:
    cp      e
    jp      z, PollLoop             ; same slot -> nothing to do

    ld      (SelectedItem), a

    push    af
    ld      a, e
    call    ToggleFavHighlight       ; remove highlight from old row
    pop     af

    call    ToggleFavHighlight       ; apply highlight to new row

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
    call    ToggleFavHighlight       ; remove highlight from old row
    pop     af
    call    ToggleFavHighlight       ; highlight the pressed item

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

    ; Save a changed default or volume before using the live filename pointer.
    ld      a, (SelectedItem)
    ld      hl, CfgDefault
    cp      (hl)
    jr      nz, .saveSettings
    ld      a, (VolumeDirty)
    or      a
    jr      z, .noSave
.saveSettings:
    push    de
    call    RunSaveIniStub          ; update default= digit in loader_fav.ini from RAM
    pop     de

.noSave:
    jp      FireCartridgeCommand

; -----------------------------------------------------------------------------
; RunSaveIniStub updates default and builds the separate save buffer before
; copying SaveIniStubRom to SAFERAM. RunSaveVolumeStub preserves the default.
; -----------------------------------------------------------------------------

RunSaveIniStub:
    ld      hl, (CfgDefaultPtr)
    ld      a, h
    or      l
    jr      z, RunSaveVolumeStub
    ld      a, (SelectedItem)
    add     a, '0'
    ld      (hl), a
RunSaveVolumeStub:
    call    PrepareVolumeSave
    ret     c
    ld      hl, SaveIniStubRom
    ld      de, SAFERAM
    ld      bc, SaveIniStubEnd - SaveIniStubRom
    ldir
    call    SAFERAM
    or      a
    ret     z
    scf
    ret

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
; Toggle one occupied row between normal and selected colours.
; In: A = slot number (0..9)
; -----------------------------------------------------------------------------

ToggleFavHighlight:
    push    af
    ld      a, (UiText)
    or      a
    jr      z, .graphic
    pop     af
    jp      FavText.DrawFavList
.graphic:
    pop     af
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

    ; Swap this file type's configured colour with the fixed background.
    call    IsDskName
    ld      a, (CfgColorRom)
    jr      nz, .haveColor
    ld      a, (CfgColorDsk)
.haveColor:
    xor     COLOR_BG
    push    af
    ld      hl, CmdFavHilite
    ld      de, FavHiliteCmd
    ld      bc, 11
    ldir
    pop     af
    ld      (FavHiliteCmd+8), a
    ld      a, (CurY)
    ld      (FavHiliteCmd+2), a
    ld      hl, FavHiliteCmd
    call    DrawBoxVDP
    call    WaitVDP

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
    ld      a, (UiText)
    or      a
    jp      nz, FavText.DrawFavList
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

    call    PrepareFavPalette
    push    bc
    push    ix

    ld      hl, MsgDigitRAM
    ld      d, 16
    ld      a, (CurY)
    ld      e, a
    ld      b, 3
    call    DrawFavStringPadded

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
    call    DrawFavStringPadded

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

; IX -> favourite, B = slot. Classify the real filename, never its label.
PrepareFavPalette:
    ld      l, (ix+0)
    ld      h, (ix+1)
    call    IsDskName
    ld      hl, FavGlyphTable
    ld      a, (CfgColorRom)
    jr      nz, .haveType
    ld      hl, FavGlyphDsk
    ld      a, (CfgColorDsk)
.haveType:
    ld      e, a
    ld      a, (SelectedItem)
    cp      b
    ld      a, e
    jr      nz, .normal
    ld      (TextBgColor), a
    ld      a, COLOR_BG
    ld      (TextColor), a
    ld      de, 1024
    add     hl, de
    jr      .ready
.normal:
    ld      (TextColor), a
    ld      a, COLOR_BG
    ld      (TextBgColor), a
.ready:
    ld      (FavBandTable), hl
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
    pop     hl
    push    de
    call    IsNextorPath
    pop     de
    jp      z, BootNextorPath

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
    ld      (MountPath), hl

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
    call    PrepareMountTimeout
    jp      SAFERAM                 ; execute the stub from RAM

SwitchStubRom:
    DISP    SAFERAM

    ld      a, (SelectedType)
    or      a
    jr      nz, .diskPath

    ld      a, CMD_MAP_FILE
    call    MAILRAM

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
    call    MAILRAM

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
.diskMountFailed:
    cp      MAIL_TIMEOUT
    jp      z, MountTimeoutRAM
    jp      MountFailed             ; original ROM still serves on failure

    ENT
SwitchStubEnd:
    ASSERT SwitchStubEnd-SwitchStubRom <= $80

; -----------------------------------------------------------------------------
; SaveIniStubRom is copied to SAFERAM and executed there. It updates the
; prepared INI in VolumeSaveBuf and rewrites /loader_fav.ini through the mailbox.
; Running from RAM lets the Z80 spin on CART_CMD while the Pico is busy with
; flash erase/program cycles, without needing the loader ROM to stay responsive.
; -----------------------------------------------------------------------------

SaveIniStubRom:
    DISP    SAFERAM

.openFile:
    ; Copy "/loader_fav.ini", 0 to CART_BUF.
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
    ld      a, (VolumeSaveSize)
    ld      c, a
    ld      a, (VolumeSaveSize+1)
    ld      b, a
    ld      a, c
    ld      (CART_BUF), a
    ld      a, b
    ld      (CART_BUF+1), a

    ld      hl, VolumeSaveBuf
    ld      de, CART_BUF+2
    ldir

    ld      a, CMD_WRITE_BLOCK
    call    .execCmd
    push    af
    ld      a, CMD_CLOSE
    call    .execCmd
    ld      b, a
    pop     af
    or      a
    ret     nz
    ld      a, b
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

    INCLUDE "joystick.inc"
    INCLUDE "mailbox.inc"
    INCLUDE "disk_options.inc"
    INCLUDE "audio_options.inc"
    INCLUDE "audio_controls.inc"
    INCLUDE "fav_volume_save.inc"

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
    call    PersistVolume
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
    INCLUDE "fav_vdp.inc"

; =============================================================================
; INI PARSER
; =============================================================================
; ParseIni scans the loaded /loader_fav.ini buffer line by line.
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

InitColors:
    ld      a, 10
    ld      (CfgColorRom), a
    ld      a, 7
    ld      (CfgColorDsk), a
    ret

; Exact keys and decimal palette indices. Invalid lines leave prior values intact.
ParseColorLine:
    push    hl
    ld      de, StrKeyColorRom
    call    MatchColorKey
    pop     hl
    ld      bc, CfgColorRom
    jr      z, .value
    push    hl
    ld      de, StrKeyColorDsk
    call    MatchColorKey
    pop     hl
    ld      bc, CfgColorDsk
    ret     nz
.value:
    ld      de, 10                 ; both key prefixes include the '='
    add     hl, de
.spaces:
    ld      a, (hl)
    cp      ' '
    jr      z, .skipSpace
    cp      9
    jr      nz, .digits
.skipSpace:
    inc     hl
    jr      .spaces
.digits:
    ld      de, 0                  ; D = value, E = digits seen
.digit:
    ld      a, (hl)
    cp      '0'
    jr      c, .endDigits
    cp      '9' + 1
    jr      nc, .endDigits
    sub     '0'
    push    bc
    ld      c, a
    ld      a, d
    add     a, a
    ld      b, a
    add     a, a
    add     a, a
    add     a, b
    add     a, c
    pop     bc
    cp      16
    ret     nc                     ; reject before any numeric overflow
    ld      d, a
    ld      e, 1                   ; at least one digit, regardless of leading zeros
    inc     hl
    jr      .digit
.endDigits:
    ld      a, e
    or      a
    ret     z
.trailing:
    ld      a, (hl)
    or      a
    jr      z, .store
    cp      13
    jr      z, .store
    cp      10
    jr      z, .store
    cp      ';'
    jr      z, .store
    cp      '#'
    jr      z, .store
    cp      ' '
    jr      z, .skipTrailing
    cp      9
    ret     nz
.skipTrailing:
    inc     hl
    jr      .trailing
.store:
    ld      a, d
    ld      (bc), a
    ret

MatchColorKey:
    ld      a, (de)
    or      a
    ret     z
    cp      (hl)
    ret     nz
    inc     hl
    inc     de
    jr      MatchColorKey

StrKeyColorRom: db "color_rom=", 0
StrKeyColorDsk: db "color_dsk=", 0

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
    cp      'k'
    jr      nz, .notKucOptions
    call    ParseKucOptionsLine
    jp      .skipToEOL
.notKucOptions:
    cp      'r'
    jr      nz, .notRomDatabase
    call    ParseRomDatabaseLine
    jp      .skipToEOL
.notRomDatabase:

    cp      'm'
    jr      nz, .notMultidisk
    call    ParseMultidiskLine
    jp      .skipToEOL
.notMultidisk:
    ; Skip section headers and comments.
    cp      '['
    jp      z, .skipToEOL
    cp      ';'
    jp      z, .skipToEOL
    cp      '#'
    jp      z, .skipToEOL

    cp      'c'
    jr      nz, .notColor
    call    ParseColorLine
    jp      .skipToEOL
.notColor:

    cp      'n'
    jr      nz, .notNextor
    call    ParseNextorLine
    jp      .skipToEOL
.notNextor:

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
    ld a, (hl)
    cp '0'
    jp c, .skipToEOL
    cp '9'+1
    jp nc, .skipToEOL
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
    ld      (CfgDelay), de
    jp      .skipToEOL              ; delay= is done; do not fall into scc_on_exit=

    ; -------------------------------------------------------------------------
    ; scc_on_exit=
    ; -------------------------------------------------------------------------
.checkS:
    call    ParseSccVolumeLine
    jp      c, .skipToEOL
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

; F1 is a matrix key, not a character: CHGET would return its BASIC macro.
; Drain that macro while held, and toggle only on a new press.
PollHelpKey:
    ld      a, 6
    call    SNSMAT
    and     $20
    jr      z, .pressed
    xor     a
    ld      (HelpF1Held), a
    ret
.pressed:
    call    KILBUF
    ld      a, (HelpF1Held)
    or      a
    jr      z, .newPress
    xor     a
    ret
.newPress:
    inc     a
    ld      (HelpF1Held), a
    or      a
    ret

ToggleHelp:
    xor a
    ld (RbscProgress), a
    ld a, (HelpActive)
    cp 3
    jp z, CloseHelp
    call    DismissVolume
    ld      a, (HelpActive)
    cp      2
    jp      z, CloseHelp
    inc     a
    ld      (HelpActive), a
    ld      (CountdownStop), a
    call    DrawCountdown
    call    DrawHelp
    jp      PollLoop

CloseHelp:
    xor     a
    ld      (HelpActive), a
    call    KILBUF
    call    DrawFrame
    call    DrawFavList
    call    DrawFooter
    jp      PollLoop

; One NUL-terminated string per line; an extra NUL ends the help table.
DrawHelp:
    ld      a, (UiText)
    or      a
    jp      nz, FavText.DrawHelp
    di
    call    ReadFwVersion
    ld      hl, CmdHelpBorder
    call    DrawBoxVDP
    ld      hl, CmdHelpFill
    call    DrawBoxVDP
    call    WaitVDP
    ld      a, COLOR_TEXT
    ld      (TextColor), a
    ld      a, COLOR_BG
    ld      (TextBgColor), a
    ld      hl, HelpLines
    ld      de, $2030              ; x=32, y=48
    ld      a, (HelpActive)
    cp      2
    jr      nz, .line
    ld      hl, CreditsLines
    ld      e, 58                   ; centre the 108-pixel credits block
.line:
    ld      a, (hl)
    or      a
    jr      z, .done
    push    hl
    push    de
    call    ResolveHelpLine
    ld      a, (HelpActive)
    cp      2
    jr      nz, .drawLine
    ; 8-pixel glyphs: x = (256 - width) / 2, independent per line.
    ld      d, 128
    push    hl
.centre:
    ld      a, (hl)
    or      a
    jr      z, .centred
    dec     d
    dec     d
    dec     d
    dec     d
    inc     hl
    jr      .centre
.centred:
    pop     hl
.drawLine:
    ld      a, (HelpActive)
    cp      2
    jr      nz, .helpLine
    ld      a, 128
    sub     d
    srl     a
    srl     a
    ld      b, a                    ; exact width, including the 25th RBSC glyph
    call    DrawStringPadded
    jr      .lineDrawn
.helpLine:
    call    DrawHelpLine
.lineDrawn:
    pop     de
    pop     hl
.next:
    ld      a, (hl)
    inc     hl
    or      a
    jr      nz, .next
    ld      a, e
    add     a, 10
    ld      e, a
    jr      .line
.done:
    ld      a, (HelpActive)
    cp      2
    call    nz, DrawFwVersion
    ei
    ret

    INCLUDE "menu_art.inc"
    INCLUDE "fw_help.inc"

DrawFrame:
    ld      a, (UiText)
    or      a
    jp      nz, FavText.DrawFrame
    di
    ld      hl, CmdShadow
    call    DrawBoxVDP
    ld      hl, CmdBorder
    call    DrawBoxVDP
    ld      hl, CmdFill
    call    DrawBoxVDP
    call    WaitVDP
    call    DrawRoundedCorners
    jp      DrawTitle

DrawCountdown:
    ; Off, or nothing to boot: blank the field once and leave it alone.
    ld      a, (CountdownStop)
    or      a
    jr      nz, .blank
    ld      a, (CfgDefault)
    cp      10
    jr      nc, .blank
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    ld      de, FavPointers
    add     hl, de
    ld      a, (hl)
    inc     hl
    or      (hl)
    jr      z, .blank

    ; Seconds = frames / 50, rounded up so the last second is visible.
    ld      hl, (CfgDelay)
    ld      c, 0
.secs:
    ld      a, h
    or      l
    jr      z, .haveSecs
    inc     c
    ld      de, 50
    or      a
    sbc     hl, de
    jr      nc, .secs
.haveSecs:
    ld      a, (LastSecs)
    cp      c
    ret     z                       ; same digit: nothing to redraw
    ld      a, c
    ld      (LastSecs), a

    ld      a, c
    ld      b, '0'
.decimal:
    cp      10
    jr      c, .numberReady
    sub     10
    inc     b
    jr      .decimal
.numberReady:
    add     a, '0'
    ld      (CountStr), a
    ld      c, a
    xor     a
    ld      (CountStr+1), a
    ld      a, b
    cp      '0'
    jr      z, .paint
    ld      (CountStr), a
    ld      a, c
    ld      (CountStr+1), a
.paint:
    ld      a, (UiText)
    or      a
    jp      nz, FavText.DrawCountdown
    di
    call    DrawCountdownBadge
    ; Centre native 8x8 digits, allowing for the font's blank right columns.
    ld      hl, 4 * 128 + 236 / 2
    ld      a, (CountStr+1)
    or      a
    jr      z, .digits
    ld      hl, 4 * 128 + 232 / 2
.digits:
    ld      a, (CountStr)
    call    DrawCountdownDigit
    ld      hl, 4 * 128 + 240 / 2
    ld      a, (CountStr+1)
    or      a
    call    nz, DrawCountdownDigit
    jr      .done

.blank:
    ld      a, (LastSecs)
    cp      $FF
    ret     z
    ld      a, $FF
    ld      (LastSecs), a
    ld      a, (UiText)
    or      a
    jp      nz, FavText.ClearCountdown
    di
    ld      hl, CmdCountdownClear
    call    DrawBoxVDP
    call    WaitVDP
.done:
    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a
    ei
    ret

    INCLUDE "countdown_badge.inc"

DrawFooter:
    ld      a, (UiText)
    or      a
    jp      nz, FavText.DrawFooter
    di
    ld      a, COLOR_FOOTER
    ld      (TextColor), a
    ld      a, COLOR_BG
    ld      (TextBgColor), a
    ld      hl, MsgInfo
    ld      d, FOOTER_X
    ld      e, FOOTER_Y
    call    DrawFooterKeys
    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a
    ei
    ret

DrawRoundedCorners:
    ld      a, COLOR_TEXT
    ld      (TextColor), a
    ld      a, COLOR_BG
    ld      (TextBgColor), a
    ld      hl, CornerTL
    ld      d, 6
    ld      e, 34
    call    DrawPattern
    ld      hl, CornerTR
    ld      d, 242
    ld      e, 34
    call    DrawPattern
    ld      hl, CornerBL
    ld      d, 6
    ld      e, 180
    call    DrawPattern
    ld      hl, CornerBR
    ld      d, 242
    ld      e, 180
    jp      DrawPattern

DrawPattern:
    push    hl
    call    FavBandVramAddr
    pop     hl

    ld      c, 8                    ; pixel lines left
.lineLoop:
    di
    push    hl
    ld      hl, (FavBandAddr)
    call    SetVramWrite
    pop     hl

    ld      a, (hl)
    inc     hl
    push    hl

    ; Corners stay white even when both file palettes are customized.
    ld      (GlyphBits), a
    push    bc
    ld      b, 4
.pair:
    call    GlyphPairToVRAM
    out     (VDP_DATA), a
    djnz    .pair
    pop     bc
    ei

    ld      hl, (FavBandAddr)
    ld      de, 128                 ; one pixel line
    add     hl, de
    ld      (FavBandAddr), hl

    pop     hl
    dec     c
    jr      nz, .lineLoop
    ret

; =============================================================================
; VDP COMMAND DATA (window for 10 rows)
; =============================================================================

CmdFavHilite:
    dw      16, 0, 208, 8          ; X, patched Y, width, glyph height
    db      11, 0, $83             ; LMMV, logical XOR

CmdShadow:
    dw      10, 38, 236, 152        ; shadow (X, Y, W, H)
    db      $11, 0, $C0

CmdBorder:
    dw      6, 34, 244, 154         ; white border
    db      $FF, 0, $C0

CmdFill:
    dw      8, 36, 240, 150         ; blue window background
    db      $44, 0, $C0

CmdHelpBorder:
    dw      16, 40, 224, 144
    db      $FF, 0, $C0
CmdHelpFill:
    dw      18, 42, 220, 140
    db      $44, 0, $C0

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

    INCLUDE "nextor/nextor_menu.inc"

MOUNT_BROWSER EQU 0
    INCLUDE "mount_errors.inc"

StrIniFile:
    db      "/loader_fav.ini", 0
StrIniFileEnd:

MsgInfo:
    db      "[F1] Help * [RET] Open", 0
CreditsLines:
    db "OmniSCC", 0
CreditsVersionLine:
    db " ", 0
    db "Created by", 0
    db "AlSp / RBSC / OSCP.club", 0
    db " ", 0
    db "Russian Bear Service Crew", 0
    db " ", 0
    db "MSX hardware", 0
    db "with passion", 0
    db " ", 0
    db "F1 / Esc: Close", 0
    db 0
HelpLines:
    db "Help", 0
    db " ", 0
    db "[RET/SPACE] Open / Start", 0
    db "[ESC] Exit from menu", 0
HelpVolumeLine:
    db "[+/-] SCC volume", 0
    db "[UP/DOWN] Select", 0
    db "[0-9] Start slot", 0
    db "[SHIFT+0-9] Start slot", 0
    db "[JOY A] Open / Start", 0
    db "[JOY B] Back / Exit", 0
    db "[F1] This help screen", 0
    db 0
CornerTL:      db $1F, $3F, $60, $C0, $C0, $C0, $C0, $C0
CornerTR:      db $F8, $FC, $06, $03, $03, $03, $03, $03
CornerBL:      db $C0, $C0, $C0, $C0, $C0, $60, $3F, $1F
CornerBR:      db $03, $03, $03, $03, $03, $06, $FC, $F8

    INCLUDE "rom_database.inc"
    INCLUDE "kuc_options.inc"
    INCLUDE "startup_screen.inc"
    INCLUDE "fav_auto_ui.inc"
LowerRomEnd:
    ASSERT $ <= $6000
    ORG $6000
    INCLUDE "mapper_tags.inc"
    MODULE FavText
    INCLUDE "fav_text.inc"
    ENDMODULE

Font8x8:
    INCLUDE "font.inc"
Font8x8End:

    ASSERT Font8x8End-Font8x8 = 2048

    INCLUDE "loader_fav_title.inc"
    INCLUDE "startup_logo.inc"

; =============================================================================
; ROM OUTPUT
; =============================================================================

RbscDrawBox EQU DrawBoxVDP
RbscWaitVDP EQU WaitVDP
RbscSetWrite EQU SetVramWrite
    INCLUDE "rbsc_easter_egg.inc"

    ASSERT  $ <= $7FFE
UpperRomEnd:
    SIZE    $4000
    SAVEBIN "../generated/loader_fav.rom", $4000, $4000
