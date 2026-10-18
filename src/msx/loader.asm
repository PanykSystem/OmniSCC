; -----------------------------------------------------------------------------
; OmniSCC Loader -- file browser
; Assembler: sjasmplus
;
; The Z80 boot menu for the OmniSCC, as a browser over the cartridge's own FAT
; volume: walk into subdirectories, scroll a list longer than the screen, pick a
; .rom or .dsk, and have that choice come back on the next power-on.
;
; This is the alternative to loader_fav.asm, whose menu is ten fixed favourites
; read from /loader.ini. The two are MUTUALLY EXCLUSIVE -- one loader.rom on the
; volume -- so this one owns /loader.ini and rewrites it canonically, keeping
; only the one thing it cannot regenerate: the name= lines that give files
; readable titles. See
; docs/superpowers/specs/2026-09-12-loader-browser-design.md.
;
; Standalone build: loader.rom; universal build: loader_auto.rom. The preloader
; discovers loader*.rom; firmware mapper detection uses the OmniSCC signature.
;
; loader_auto.asm selects SCREEN 1 or SCREEN 5 inside this same browser.
; Required files for the graphical variant: font.inc, vdp.inc, mailbox.inc (shared with
; loader_fav.asm, which is why they are includes and not copies).
; -----------------------------------------------------------------------------

    DEVICE NOSLOT64K
    ORG     $4000

; =============================================================================
; PICO MAILBOX
; =============================================================================
; Command mailbox in cartridge address space. Every command releases /WAIT and
; leaves the cartridge serving nothing while its handler runs, which is why the
; poll lives in MSX RAM and tests two addresses; see mailbox.inc.

CART_BUF        EQU $6000
CART_CMD        EQU $7FFF
CART_STATUS     EQU $7FFE
READY_SENTINEL  EQU $A5

CMD_MAP_FILE    EQU $01
CMD_MAP_SCC     EQU $02
CMD_MAP_DISK    EQU $03
CMD_OPEN_READ   EQU $10
CMD_OPEN_WRITE  EQU $11
CMD_READ_BLOCK  EQU $12
CMD_WRITE_BLOCK EQU $13
CMD_CLOSE       EQU $14
CMD_DIR_OPEN    EQU $20
CMD_DIR_NEXT    EQU $21
CMD_DIR_CLOSE   EQU $23

; =============================================================================
; BIOS
; =============================================================================

CHGMOD          EQU $005F
CHGCLR          EQU $0062
INITXT          EQU $006C
FILVRM          EQU $0056
KILBUF          EQU $0156
CHSNS           EQU $009C
CHGET           EQU $009F
SNSMAT          EQU $0141
CHPUT           EQU $00A2

HIMEM           EQU $FC4A    ; top of free RAM: everything this loader uses in
                             ; page 3 has to stay below it, because the vendor
                             ; Disk ROM's buffers are above

; And not merely below it: the stack is there too. A cartridge that reserves RAM
; lowers HIMEM and moves the stack down with it, which is what the vendor Disk
; ROM's INIT did before this loader's INIT ran -- so on a disk machine SP sits
; just under HIMEM's value, and this loader keeps using it.
;
; The entry table therefore has to stop short of HIMEM by more than the deepest
; stack this code can produce. Its own worst path is about 40 bytes; the BIOS
; interrupt handler, which runs on this stack 50 times a second because PollLoop
; halts, is good for another 80. 512 is four times that.
;
; Without the reserve the last entries of a full listing and the stack were the
; same bytes: a modelled walk into a 200-file directory had the table written up
; to 0xEDEA with the stack down at 0xEDEC, and that is with no interrupt handler
; in the model. On hardware the handler's saved registers were inside the table,
; so a listing corrupted whatever the interrupted code held in its registers --
; and an interrupted glyph renderer writes its next bytes wherever the wrecked
; pointer happens to land. That is what a striped screen after entering a large
; directory was.
STACK_RESERVE   EQU 512
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
; The loader owns the machine while it runs -- BASIC has not cold-started and no
; disk ROM has allocated anything yet -- so page 3 below the BIOS work area is
; ours. The map is laid out so nothing overlaps:
;
;   C000-C7FF   /loader.ini as read from flash
;   C800-C8FF   variables
;   C900-C92F   MailPoll, planted by Init (mailbox.inc)
;   C930-C9FF   path scratch
;   CA00-CAFF   stubs that outlive the ROM
;   CB00-CEFF   glyph table for files, built at startup
;   CF00-D2FF   glyph table for directories, ditto
;   D300-D31F   the row being drawn
;   D320-D417   sort order: one byte per entry, indices into the table
;   D418-D47F   late variables: diskrom=, which outgrew page C8
;   D480-        entry table, ENTRY_SIZE bytes per entry, ending below both HIMEM
;                and the BIOS stack pointer saved at entry
;
; The ini buffer is not scratch: it holds the name= lines for the whole session,
; because that is where the labels ENTRY_DISP points at live.
;
; EVERYTHING HAS TO STAY BELOW HIMEM. This loader runs during the BIOS slot
; scan, and on a machine with a disk interface the vendor Disk ROM's INIT has
; already reserved its sector buffers and DPBs directly under HIMEM -- which is
; usually somewhere around EE00-F000, not F380. An earlier version of this map
; put the glyph table at ED00 and wrote a kilobyte straight through those
; buffers: the machine came up as a blue BASIC screen with a blinking cursor.
;
; So the only variable-sized area, the entry table, is the last one. Its cap
; uses the lower of HIMEM and the stack pointer saved at entry, minus
; STACK_RESERVE. The BIOS stack need not sit immediately under HIMEM.

RAM_INI_BUF     EQU $C000
INI_BUF_MAX     EQU 2047
; What PreserveNameLines leaves free for the five keys SaveIni appends after the
; labels: last=, disk2= and diskrom= are about 72 bytes each at worst and the
; other two under 30, so 245 is the real worst case and this is the round number
; above it.
INI_KEYS_RESERVE EQU 512

SavedSP         EQU $C800
IniSize         EQU $C802
CfgDelay        EQU $C804    ; countdown, in frames, counted down to zero
CfgDelayMs      EQU $C8CE    ; the configured delay in milliseconds, untouched
                             ; by the countdown -- this is what gets saved
VolumeFrames    EQU $D4B8
VolumeDirty     EQU $D4B9
RbscProgress    EQU $D4BA    ; next letter of the hidden keyboard sequence
DiskStatusVisible EQU $D4BB  ; temporary B: footer, dismissed by the next input
VolumeText      EQU RowBuf
VOLUME_BROWSER EQU 1
CfgSccVolume    EQU $D4B7
CfgMultidisk    EQU $D4B6
CfgSccOnExit    EQU $C806
SelectedType    EQU $C807    ; 0 = ROM entry, 1 = .dsk image
SelIndex        EQU $C808    ; highlighted entry, 0-based
ScrollTop       EQU $C80A    ; first entry drawn in the window
EntryCount      EQU $C80C
EntryCapped     EQU $C80E    ; non-zero: the directory had more than the cap
DirError        EQU $C810    ; non-zero: this directory would not open
CountdownStop   EQU $C80F
TextX           EQU $C811
TextY           EQU $C812
TextColor       EQU $C813
TextBgColor     EQU $C814
GlyphBits       EQU $C815
CurY            EQU $C816
ScanIdx         EQU $C817    ; running count while a directory is read
EntryCap        EQU $C8FA    ; how many entries fit below HIMEM, capped at
                             ; ENTRY_MAX -- see ComputeEntryCap
BandTable       EQU $C8FC    ; which glyph table DrawBand is painting from
BuildFg         EQU $C8FE    ; foreground colour BuildOneTable is baking in
NamePtr         EQU $D31B    ; AcceptEntry's pointer to the name it will store,
                             ; here because page C8 is full to the last byte
SortI           EQU $C819    ; insertion-sort cursors
SortJ           EQU $C81B
AutoBoot        EQU $C81D    ; non-zero: the countdown should boot LastPath
BootFailed      EQU $C81E    ; BOOT_FAILED_MARK after a failed mount; see Init
JoystickState   EQU $C820    ; five bytes, shared joystick polling state
HelpActive      EQU $C825    ; modal help is visible
HelpF1Held      EQU $C826    ; one toggle per physical F1 press
ZeroSeen        EQU $C81F    ; AppendNumber's leading-zero state
BOOT_FAILED_MARK EQU $A5
Disk2Path       EQU $C830    ; disk2=, 65 bytes: the image in drive B
LastPath        EQU $C880    ; last=, 65 bytes, so C880..C8C0

; Above LastPath's 65 bytes, not inside them. These three started at C8B0, which
; is the 48th byte of LastPath: reading a directory wrote AliasPtr into the
; middle of a long last= path and booted whatever the wreckage named.
; test_loader_logic.py checks the whole map for overlaps now.
AliasPtr        EQU $C8C2    ; AcceptEntry's pointer to the 8.3 alias
SortSlot        EQU $C8C4    ; &SortOrder[j] while one element sinks
LeftPtr         EQU $C8C6    ; the entry SortOrder[j-1] names
LastSecs        EQU $C8C8    ; seconds last shown by the countdown
CountStr        EQU $C8CA    ; seconds as decimal digits, 4 bytes
PrevSel         EQU $C8D0    ; selection and window as last PAINTED, so MoveTo
PrevTop         EQU $C8D2    ; can tell a scroll from a cursor step
NewSel          EQU $C8D4    ; where the current move is taking the cursor
PaintPending    EQU $C8D6    ; non-zero: moves applied but not yet painted
HiliteCmd       EQU $C8D8    ; 11-byte VDP block, DY patched per row
ShiftCmd        EQU $C8E4    ; 15-byte VDP block, SY and DY patched per row
BandText        EQU $C8F4    ; DrawBand's cursor over the row text
BandAddr        EQU $C8F6    ; the VRAM address of the line being written
BandLine        EQU $C8F8    ; which of the eight pixel lines that is
BandCount       EQU $C8F9    ; characters in the band

MAILRAM         EQU $C900    ; MAILPOLL_SIZE bytes: the poll is copied here
CurPath         EQU $C930    ; directory being listed, 65 bytes, starts "/"
FullPath        EQU $C980    ; CurPath + "/" + name, 96 bytes
SAFERAM         EQU $CA00

; Four VRAM bytes for each of the 256 bit patterns a glyph line can hold, built
; at startup by BuildGlyphTable. One kilobyte to make the renderer five times
; faster; see DrawBand.
;
; A fixed UI table and a reusable entry table, rebuilt on foreground changes.
; This avoids reserving an extra kilobyte at the expense of directory capacity.
GlyphTable      EQU $CB00
GlyphTableDir   EQU $CF00

; The row being drawn, assembled as one string so a whole row goes out as a
; single band: marker, name, the scroll hint in the last column, and the padding
; that erases what was there before.
RowBuf          EQU $D300

; One byte per entry, indices into the table.
SortOrder       EQU $D320

; Late variables. Page C8 is full to its last byte and every area below is
; fixed, so anything that grows afterwards lands here, between the sort order
; and the entry table -- which costs entries, two of them, and is the only room
; left.
;
; diskrom= is here because fifteen characters was not a limit anyone could have
; guessed at: `diskrom=yis805-128r2_disk.rom` was silently cut to
; `yis805-128r2_di`, the mount then failed, and saving wrote the truncation back
; to the ini. It takes a whole path now, so the dump can live in a folder.
DiskRomName     EQU $D418    ; diskrom=, DISKROM_BUF_SIZE bytes

; And the table itself, which is the only part whose size is decided at run
; time -- see ComputeEntryCap.
NextorRomName   EQU $D459
NextorInvalid   EQU $D49B
CfgColorDir     EQU $D49E
CfgColorRom     EQU $D49F
CfgColorDsk     EQU $D4A0
RowColor        EQU $D4A1
FwHelpText      EQU $D4A2    ; 16 bytes, below EntryTable
NextorLastError EQU $D49C
NEXTOR_BROWSER EQU 1

    IFDEF ROM_DATABASE_MENU
CfgKucOptions   EQU $D501
RomDatabasePath EQU $D4C0
EntryTable      EQU $D508
    ELSE
EntryTable      EQU $D4C0
    ENDIF

; One entry: an attribute, what it is listed as, and the name to open.
;
; The name is stored whole and truncated only when it is drawn. It used to be
; kept twice -- truncated for display, 8.3 alias for opening -- and opening by
; the alias threw away the mapper tag a filename carries: `game[Konami].rom`
; opens as `GAME~1.ROM`, and the firmware's DetectCartType then sees no tag and
; guesses the mapper instead of being told. A name too long even for this field
; falls back to the alias, which is the only case where one is needed at all.
;
; ENTRY_DISP points at the label a `name=` line gave this file, inside the ini
; buffer, or is zero when there is none. A pointer rather than a copy: the ini
; stays in RAM all session and a second 32-byte string per entry would halve how
; many entries fit. The label is what the row shows and what the sort orders by;
; the name underneath is still what gets opened.
ENTRY_SIZE      EQU 36       ; 35 in use, and 36 = 32 + 4 makes EntryAddr cheap
ENTRY_ATTR      EQU 0
ENTRY_DISP      EQU 1        ; pointer into the ini buffer, 0 = no label
ENTRY_NAME      EQU 3        ; NAME_MAX characters + NUL
NAME_MAX        EQU 31
; The ceiling on the runtime cap: SortOrder holds one byte per entry, and a byte
; index cannot address more than this many.
ENTRY_MAX       EQU 248

; Entry attributes. 0 and 1 are what the firmware reports; 2 is ours, for the
; ".." row the browser puts at the top of every directory but the root.
ATTR_FILE       EQU 0
ATTR_DIR        EQU 1
ATTR_PARENT     EQU 2

PATH_MAX        EQU 64
; A path, not a bare name: the dump may live in a folder, and 15 characters did
; not even hold the name of one of the two dumps this project is tested against.
DISKROM_NAME_MAX EQU PATH_MAX
DISKROM_BUF_SIZE EQU DISKROM_NAME_MAX + 1

; Screen geometry: sixteen 8-pixel rows with one pixel of separation.
; The final row ends at 183, inside CmdFill (y=36..185).
LIST_ROWS       EQU 16
LIST_Y0         EQU 40
LIST_PITCH      EQU 9
LIST_X          EQU 16
LIST_WIDTH      EQU 26       ; characters per row, including the folder symbol
; The scroll hints sit in their own column to the RIGHT of the list, outside the
; rectangle the block move shifts. Inside it they rode along with the pixels: a
; `v` drawn on the bottom row stayed visible on every row it was scrolled up
; through, and the arrows accumulated down the window.
HINT_X          EQU LIST_X + LIST_WIDTH * 8


; Fixed UI colors. Entry foregrounds are configured independently in the INI.
; XOR highlighting is reversible for every entry color.
COLOR_TEXT      EQU 15
COLOR_FOOTER    EQU 10
COLOR_BG        EQU 4
HILITE_MASK     EQU COLOR_TEXT ^ COLOR_BG
    IFDEF AUTO_LOADER
FOOTER_X        EQU 40       ; centered 22-character Help/Open hint
FOOTER_Y        EQU 200      ; shared Help/Open and temporary drive B status
    ELSE
FOOTER_X        EQU 32
FOOTER_Y        EQU 192
    ENDIF
FOOTER_CHARS    EQU 24       ; what DrawString8x8 draws, and what fits the
                             ; line below the box, like loader_fav.asm's
                             ; hint line
COUNT_X         EQU 226      ; fixed 24x12 badge, clear of title and path
COUNT_Y         EQU 2
TITLE_X         EQU 8
TITLE_Y         EQU 2
PATH_X          EQU 16
PATH_Y          EQU 24
PATH_CHARS      EQU 28


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
    IFDEF AUTO_LOADER
    call    DetectUi
    ENDIF
    ld      (SavedSP), sp

    ; The mailbox poll goes into RAM before any command is sent: a command stops
    ; the cartridge answering the bus, so the loop that waits for it cannot be
    ; fetched from this ROM. See mailbox.inc.
    ld      hl, MailPollRom
    ld      de, MAILRAM
    ld      bc, MailPollRomEnd - MailPollRom
    ldir

    call    ClearConfig
    ld      a, (BootFailed)
    cp      BOOT_FAILED_MARK
    call    nz, InitJoystick
    call    InitHiliteCmd
    call    ComputeEntryCap

    ; Screen 5 through the BIOS, colours, then interrupts off for rendering.
    IFDEF AUTO_LOADER
    call    InitAutoScreen
    ELSE
    xor     a
    ld      (DPPAGE), a
    ld      (ACPAGE), a

    ld      a, 5
    call    CHGMOD
    ENDIF

    ld      a, 15
    ld      (FORCLR), a
    ld      a, 4
    ld      (BAKCLR), a
    ld      (BDRCLR), a
    call    CHGCLR

    IFDEF AUTO_LOADER
    call    DrawStartupSplash
    ENDIF

    di

    ; Read and parse /loader.ini. A missing or empty file is not an error: the
    ; browser opens at the root with the default delay and no auto-boot.
    call    LoadIniFile
    ld      a, b
    or      c
    jr      z, .noIni

    ld      hl, RAM_INI_BUF
    call    ParseIni

    ; Everything parsed is in variables now, so the buffer can be reduced to the
    ; one thing still read out of it: the name= lines. FindLabel runs once per
    ; listed file and walks this buffer to its end, so leaving the whole file
    ; there would make reading a directory cost the whole file per entry.
    call    PreserveNameLines
    xor     a
    ld      (de), a

.noIni:
    IFDEF ROM_DATABASE_MENU
    call    ApplyRomDatabase
    call    ApplyKucOptions
    ENDIF
    call    ApplyDiskOptions
    IFDEF AUTO_LOADER
    call    ApplySccVolume
    ENDIF
    call    BuildGlyphTable
    ; Milliseconds to VBlank frames (~50 Hz), as loader_fav.asm does it. The
    ; milliseconds stay where they are: the countdown consumes CfgDelay, and
    ; saving reads CfgDelayMs.
    ld      hl, (CfgDelayMs)
    ld      bc, 0
.div20:
    ld      de, 20
    or      a
    sbc     hl, de
    jr      c, .divDone
    inc     bc
    jr      .div20
.divDone:
    IFDEF AUTO_LOADER
    ld a, b
    or c
    jr nz, .storeFrames
    ld hl, (CfgDelayMs)
    ld a, h
    or l
    jr z, .storeFrames
    inc bc
.storeFrames:
    ENDIF
    ld      (CfgDelay), bc

    ; A mount that just failed came back here through the stub. Do not count
    ; down onto the same entry again -- that is a boot loop with a message in it.
    ld      a, (BootFailed)
    cp      BOOT_FAILED_MARK
    jr      nz, .autoOk
    xor     a
    ld      (BootFailed), a
    ld      (AutoBoot), a
    ld      a, 1
    ld      (CountdownStop), a

.autoOk:
    ; Start in the directory last= lives in, so the browser opens where it was
    ; left. An empty or rootless last= starts at the root.
    call    StartDirFromLast
    IFDEF AUTO_LOADER
    call    DisableZeroDelay
    ENDIF

    ; Preserve the splash while preparing the menu's hidden page.
    IFDEF AUTO_LOADER
    call    PrepareStartupMenu
    ELSE
    xor     a
    ld      (DPPAGE), a
    ld      (ACPAGE), a
    ld      a, 5
    call    CHGMOD
    ENDIF

    ld      a, 15
    ld      (FORCLR), a
    ld      a, 4
    ld      (BAKCLR), a
    ld      (BDRCLR), a
    call    CHGCLR

    di

    call    DrawFrame
    call    ReadDirectory
    call    ResetSelection
    IFDEF AUTO_LOADER
    call    RestoreLastSelection
    ENDIF
    call    DrawList
    call    DrawFooter
    call    DrawCountdown

    IFDEF AUTO_LOADER
    call    ShowStartupScreen
    call    ResetVramPage
    ELSE
    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a

    ENDIF
    ei

    ; Let the display settle before the first poll, as loader_fav.asm does.
    ld      b, 5
.settle:
    halt
    djnz    .settle

; =============================================================================
; MAIN LOOP
; =============================================================================

PollLoop:
    halt
    IFDEF AUTO_LOADER
    call    TickVolume
    ENDIF
    call    PollHelpKey
    jp      nz, ToggleHelp
    ld      a, (HelpActive)
    or      a
    jr      z, .browser
    call    CHSNS
    jp      nz, DispatchKey
    jr      PollLoop
.browser:

    ; Read input before advancing any enabled countdown.
    call    PollJoystick
    jr      nz, .gotJoystick
    call    CHSNS
    jr      nz, .gotKey

    ld      a, (CountdownStop)
    or      a
    jr      nz, PollLoop

    ; Only count down if there is something to boot.
    ld      a, (AutoBoot)
    or      a
    jr      z, PollLoop

    ld      de, (CfgDelay)
    ld      a, d
    or      e
    jr      z, .expired

    dec     de
    ld      (CfgDelay), de
    ld      a, d
    or      e
    jr      z, .expired
    call    DrawCountdown
    jr      PollLoop

.expired:
    ; The countdown boots what last= named, not what the cursor happens to be
    ; on: the saved choice is the one the user made deliberately.
    ld      hl, LastPath
    call    BootPath

.gotJoystick:
    IFDEF AUTO_LOADER
    call    DismissDiskStatus
    ENDIF
    cp      8
    jp      z, JoystickBack
    jp      DispatchInput

.gotKey:
; -----------------------------------------------------------------------------
; DispatchKey reads one key and acts on it. MoveTo jumps back here while more
; keys are still queued, so a burst is consumed before anything is painted.
; -----------------------------------------------------------------------------
DispatchKey:
    call    CHGET
    IFDEF AUTO_LOADER
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
    ENDIF


DispatchInput:
    ld      b, a
    ld      a, (HelpActive)
    or      a
    ld      a, b
    jr      z, .browser
    cp      $1B
    jp      z, CloseHelp
    jp      PollLoop
.browser:
    IFDEF AUTO_LOADER
    call    DismissDiskStatus
    ENDIF
    push    af
    ld      a, 1
    ld      (CountdownStop), a
    call    DrawCountdown
    pop     af
    IFDEF AUTO_LOADER
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
    ENDIF
    cp      $1B
    jp      z, ExitToBasic          ; ESC
    cp      13
    jp      z, EnterSelected        ; RETURN
    cp      ' '
    jp      z, EnterSelected        ; SPACE
    cp      8
    jp      z, GoParent             ; BS
    cp      30
    jr      z, .up
    cp      31
    jr      z, .down
    cp      29
    jr      z, .pageUp              ; cursor LEFT
    cp      28
    jr      z, .pageDown            ; cursor RIGHT
    cp      $12
    jp      z, .setDisk2            ; INS assigns B
    cp      $7F
    jp      z, .clearDisk2          ; DEL clears B
    jp      PollLoop

.up:
    ld      hl, (SelIndex)
    ld      a, h
    or      l
    jp      z, PollLoop             ; already at the top
    dec     hl
    jp      MoveTo

.down:
    ld      hl, (SelIndex)
    inc     hl
    ld      de, (EntryCount)
    call    CmpHLDE
    jp      nc, PollLoop            ; past the last entry
    jp      MoveTo

.pageUp:
    ld      hl, (SelIndex)
    ld      de, LIST_ROWS
    or      a
    sbc     hl, de
    jr      nc, .pageUpOk
    ld      hl, 0
.pageUpOk:
    jp      MoveTo

.pageDown:
    ld      hl, (SelIndex)
    ld      de, LIST_ROWS
    add     hl, de
    ld      de, (EntryCount)
    call    CmpHLDE
    jr      c, .pageDownOk
    ; Clamp to the last entry. EntryCount is zero only when the list is empty,
    ; and then SelIndex stays where it was.
    ld      hl, (EntryCount)
    ld      a, h
    or      l
    jp      z, PollLoop
    dec     hl
.pageDownOk:
    jp      MoveTo

.setDisk2:
    ; INS assigns the highlighted .dsk to B. It never toggles the assignment;
    ; DEL clears B independently of the highlighted entry.
    ld      hl, (EntryCount)
    ld      a, h
    or      l
    jp      z, PollLoop

    ld      bc, (SelIndex)
    call    SortedEntryAddr
    ld      a, (hl)
    or      a
    jp      nz, PollLoop            ; a directory, or the ".." row

    call    BuildFullPath
    ld      hl, FullPath
    call    IsDskPath
    jp      nz, PollLoop

    ld      hl, FullPath
    call    StrLen
    ld      a, b
    cp      PATH_MAX + 1
    jp      nc, PollLoop            ; too long for the ini's field

    ld      hl, FullPath
    ld      de, Disk2Path
    call    StrEqNoCase
    jp      z, PollLoop            ; repeated INS does not rewrite flash
    ld      hl, FullPath
    ld      de, Disk2Path
    call    StrCopy
    jr      .disk2Saved

.clearDisk2:
    ld      a, (Disk2Path)
    or      a
    jp      z, PollLoop            ; already empty: avoid a flash rewrite
    xor     a
    ld      (Disk2Path), a

.disk2Saved:
    ; Written out now rather than at boot, so the assignment survives an exit to
    ; BASIC. Safe in mid-session only because PreserveNameLines is idempotent:
    ; the buffer was compacted at startup, so this pass copies each name= line
    ; onto itself and the label pointers the list holds stay valid.
    call    SaveIni
    IFDEF AUTO_LOADER
    call    DrawList
    ENDIF
    call    DrawDisk2
    jp      PollLoop

; -----------------------------------------------------------------------------
; MoveTo puts the selection at HL and paints the least it can.
;
; Three things keep this off the glyph renderer, which costs about 1.8 ms per
; character cell and made a 12-row repaint a third of a second:
;
;   - The highlight moves as two XOR rectangles (HiliteRow), not as two
;     repainted rows. No glyph is touched at all.
;   - A one-row scroll shifts the window's pixels with one VDP block move
;     (ShiftList) and then draws only the row that was vacated.
;   - A burst of queued keypresses -- which is what a held arrow produces -- is
;     applied in full and painted once, rather than painted per repeat.
;
; Only a jump of more than one row, which means paging, still repaints
; everything.
; -----------------------------------------------------------------------------
MoveTo:
    ld      (NewSel), hl

    ; The snapshot is the state on the screen, so it is taken once per burst
    ; rather than once per key.
    ld      a, (PaintPending)
    or      a
    jr      nz, .haveSnapshot
    ld      hl, (SelIndex)
    ld      (PrevSel), hl
    ld      hl, (ScrollTop)
    ld      (PrevTop), hl
    ld      a, 1
    ld      (PaintPending), a

.haveSnapshot:
    ld      hl, (NewSel)
    ld      (SelIndex), hl
    call    ClampScroll

    ; Another key already waiting? Apply it before painting anything.
    call    CHSNS
    jp      nz, DispatchKey

    xor     a
    ld      (PaintPending), a

    IFDEF AUTO_LOADER
    ld      a, (UiText)
    or      a
    jr      z, .graphicMove
    call    DrawList
    jp      PollLoop
.graphicMove:
    ENDIF
    ; Did the window move, and by how much?
    ld      hl, (ScrollTop)
    ld      de, (PrevTop)
    call    CmpHLDE
    jr      z, .sameWindow

    ; One row down: the contents move up and the last row is new.
    ld      hl, (PrevTop)
    inc     hl
    ld      de, (ScrollTop)
    call    CmpHLDE
    jr      nz, .maybeUp
    ld      a, 0
    jp      .shifted

.maybeUp:
    ; One row up: the contents move down and the first row is new.
    ld      hl, (ScrollTop)
    inc     hl
    ld      de, (PrevTop)
    call    CmpHLDE
    jr      nz, .full
    ld      a, 1

.shifted:
    call    ShiftList               ; A selects the direction
    ld      hl, (SelIndex)
    call    DrawRow                 ; the row the shift vacated
    ld      hl, (PrevSel)
    call    HiliteRow               ; its highlight was shifted along: undo it
    ld      hl, (SelIndex)
    call    HiliteRow
    call    DrawScrollHints         ; their column does not move with the pixels
    jp      PollLoop

.full:
    call    DrawList
    jp      PollLoop

.sameWindow:
    ld      hl, (PrevSel)
    call    HiliteRow               ; off the row the cursor left
    ld      hl, (SelIndex)
    call    HiliteRow               ; onto the row it is on now
    jp      PollLoop


; =============================================================================
; SELECTION AND NAVIGATION
; =============================================================================

; -----------------------------------------------------------------------------
; ClampScroll keeps ScrollTop such that SelIndex is one of the LIST_ROWS rows
; drawn from it.
; -----------------------------------------------------------------------------
ClampScroll:
    ld      hl, (SelIndex)
    ld      de, (ScrollTop)
    call    CmpHLDE
    jr      nc, .notAbove
    ld      (ScrollTop), hl         ; selection above the window: scroll to it
    ret

.notAbove:
    ; Below the window? ScrollTop = SelIndex - (LIST_ROWS - 1).
    ld      hl, (ScrollTop)
    ld      de, LIST_ROWS - 1
    add     hl, de                  ; HL = last visible row
    ld      de, (SelIndex)
    call    CmpHLDE
    ret     nc                      ; last visible >= selection: nothing to do

    ld      hl, (SelIndex)
    ld      de, LIST_ROWS - 1
    or      a
    sbc     hl, de
    ld      (ScrollTop), hl
    ret

ResetSelection:
    xor     a
    ld      (PaintPending), a       ; RAM may contain FF at power-on
    ld      hl, 0
    ld      (SelIndex), hl
    ld      (ScrollTop), hl
    ret

; -----------------------------------------------------------------------------
; EnterSelected: a directory is entered, a file is booted.
; -----------------------------------------------------------------------------
EnterSelected:
    ld      hl, (EntryCount)
    ld      a, h
    or      l
    jp      z, PollLoop             ; empty directory

    ld      bc, (SelIndex)
    call    SortedEntryAddr         ; sorted position, not table index
    ld      a, (hl)                 ; attribute
    cp      ATTR_PARENT
    jp      z, GoParent             ; the ".." row
    or      a
    jr      nz, .isDir

    ; A file: build the full path and boot it.
    call    BuildFullPath
    ld      hl, FullPath
    jp      BootPath

.isDir:
    ; Append "/NAME" to CurPath and list it. The 8.3 alias is what gets
    ; appended, for the same reason a file is opened by it.
    call    AppendEntryToPath
    call    ReadDirectory
    call    ResetSelection
    call    DrawPath
    call    DrawList
    call    DrawFooter
    jp      PollLoop

; -----------------------------------------------------------------------------
; GoParent: truncate CurPath at its last '/'. At the root it does nothing.
; -----------------------------------------------------------------------------
JoystickBack:
    ld      a, (CurPath+1)
    or      a
    jp      z, ExitToBasic
    jp      GoParent

GoParent:
    ld      hl, CurPath
    ld      de, 0                   ; DE = address of the last '/', 0 if none
.scan:
    ld      a, (hl)
    or      a
    jr      z, .scanned
    cp      '/'
    jr      nz, .next
    ld      d, h
    ld      e, l
.next:
    inc     hl
    jr      .scan

.scanned:
    ld      a, d
    or      e
    jp      z, PollLoop             ; no '/' at all: nothing to go up to

    ex      de, hl                  ; HL -> the last '/'
    ld      de, CurPath
    call    CmpHLDE
    jr      z, .atRoot

    ld      (hl), 0                 ; "/a/b" -> "/a"
    jr      .reload

.atRoot:
    ; The last slash is the leading one: "/name" becomes "/", and "/" itself has
    ; nowhere to go.
    inc     hl
    ld      a, (hl)
    or      a
    jp      z, PollLoop
    ld      (hl), 0

.reload:
    call    ReadDirectory
    call    ResetSelection
    call    DrawPath
    call    DrawList
    call    DrawFooter
    jp      PollLoop

; =============================================================================
; DIRECTORY LISTING
; =============================================================================

; -----------------------------------------------------------------------------
; ReadDirectory fills EntryTable from CurPath: one mailbox round trip per entry,
; filtered to directories and *.ROM / *.DSK, then sorted.
;
; A directory of 255 entries therefore costs 255 round trips -- tens to hundreds
; of milliseconds -- so "Reading..." goes up first. In exchange, every scroll
; afterwards touches nothing but RAM: CMD_DIR_SEEK rewinds and re-walks, so
; paging through it would pay that cost on every keypress instead of once.
; -----------------------------------------------------------------------------
ReadDirectory:
    call    DrawReading

    ld      hl, 0
    ld      (EntryCount), hl
    xor     a
    ld      (EntryCapped), a
    ld      (DirError), a
    ld      hl, 0
    ld      (ScanIdx), hl

    ; Unless this is the root, the list starts with "..". Entering it goes up,
    ; which is the idiom everyone already knows; BS still does the same thing.
    ld      a, (CurPath + 1)
    or      a
    jr      z, .noParent

    ld      bc, 0
    call    EntryAddr
    ld      (hl), ATTR_PARENT       ; sorts ahead of every real entry
    inc     hl
    ld      (hl), 0                 ; no label: ".." is already readable
    inc     hl
    ld      (hl), 0
    inc     hl
    ld      (hl), '.'
    inc     hl
    ld      (hl), '.'
    inc     hl
    ld      (hl), 0

    ld      hl, 1
    ld      (EntryCount), hl

.noParent:
    ; Path into the buffer, then open.
    ld      hl, CurPath
    ld      de, CART_BUF
    call    StrCopy

    ld      a, CMD_DIR_OPEN
    call    ExecCartCmd
    or      a
    jr      nz, .openFailed

.nextEntry:
    ld      hl, (EntryCount)
    ld      de, (EntryCap)
    call    CmpHLDE
    jr      c, .haveRoom
    ld      a, 1
    ld      (EntryCapped), a        ; the rest of the directory is not listed
    jr      .done

.haveRoom:
    ld      a, CMD_DIR_NEXT
    call    ExecCartCmd
    or      a
    jr      nz, .done               ; 0x01 = end of directory, anything else an error

    call    AcceptEntry
    jr      .nextEntry

.done:
    ld      a, CMD_DIR_CLOSE
    call    ExecCartCmd
    call    SortEntries
    call    ClearReading
    ret

.openFailed:
    ld      a, 1
    ld      (DirError), a           ; DrawFooter says so
    call    SortEntries             ; leaves the ".." row, if there is one
    call    ClearReading
    ret

; -----------------------------------------------------------------------------
; AcceptEntry copies the reply at CART_BUF into the table if it passes the
; filter. The reply is: attribute, long name (NUL), 8.3 alias (NUL, empty when
; it is the same as the long name).
; -----------------------------------------------------------------------------
AcceptEntry:
    ; Find the alias, which is what everything downstream uses.
    ld      hl, CART_BUF + 1
    call    StrEnd                  ; HL -> the long name's terminator
    inc     hl                      ; HL -> alias
    ld      a, (hl)
    or      a
    jr      nz, .haveAlias
    ld      hl, CART_BUF + 1        ; empty alias: the long name is already 8.3
.haveAlias:
    ld      (AliasPtr), hl

    ; Prefer the name as it is: it carries the mapper tag, and FatFs opens long
    ; names perfectly well. Only a name that will not fit the entry falls back to
    ; the alias, which always fits.
    ld      hl, CART_BUF + 1
    call    StrLen
    ld      a, b
    cp      NAME_MAX + 1
    jr      c, .haveName
    ld      hl, (AliasPtr)
.haveName:
    ld      (NamePtr), hl

    ld      a, (CART_BUF)
    or      a
    jr      nz, .store              ; a directory is always listed

    ; Files: only .ROM and .DSK, and never the two that cannot be booted as a
    ; cartridge -- the loader's own ROM and whatever diskrom= names.
    ld      hl, (NamePtr)
    call    IsBootable
    ret     nz

.store:
    ld      bc, (EntryCount)
    call    EntryAddr               ; HL -> the slot
    push    hl

    ld      a, (CART_BUF)
    ld      (hl), a                 ; attribute

    ; The ini may give this file a readable title. Only the pointer is stored;
    ; the filename underneath stays what gets opened.
    call    FindLabel               ; HL = the label, or 0
    ex      de, hl                  ; DE = the label
    pop     hl
    push    hl
    inc     hl
    ld      (hl), e
    inc     hl
    ld      (hl), d

    pop     hl
    ld      de, ENTRY_NAME
    add     hl, de
    ex      de, hl                  ; DE -> the slot's name
    ld      hl, (NamePtr)
    ld      b, NAME_MAX
    call    StrCopyN

    ld      hl, (EntryCount)
    inc     hl
    ld      (EntryCount), hl
    ret

; -----------------------------------------------------------------------------
; FindLabel looks up the name at (NamePtr) among the ini's name= lines.
;
;   out HL = the label, or 0 when there is none for this file
;
; A line is `name=<filename>,<label>`, and it is matched on the filename alone,
; with no path: what the user sees in the browser is what they write in the ini.
; The same label therefore applies to that filename in every directory, which is
; the trade for being able to write these by hand.
;
; Nothing is copied. The returned pointer is into the ini buffer, which stays
; untouched until SaveIni -- see PreserveNameLines, which runs after the last
; repaint for exactly that reason.
; -----------------------------------------------------------------------------
FindLabel:
    ld      hl, RAM_INI_BUF
.nextLine:
    ld      a, (hl)
    or      a
    jr      z, .none
    cp      13
    jr      z, .skipChar
    cp      10
    jr      z, .skipChar
    cp      ' '
    jr      z, .skipChar
    cp      9
    jr      z, .skipChar

    ; A line starts here. Only name= lines can match.
    call    IsNameLine
    jr      nz, .toEOL

    push    hl
    call    FindValue               ; HL past the '=' and any spaces
    jr      c, .noValue
    call    MatchLabelValue         ; carry clear: HL -> the label
    jr      nc, .found
.noValue:
    pop     hl

.toEOL:
    ld      a, (hl)
    or      a
    jr      z, .none
    inc     hl
    cp      13
    jp      z, .nextLine
    cp      10
    jp      z, .nextLine
    jr      .toEOL

.skipChar:
    inc     hl
    jp      .nextLine

.found:
    pop     de                      ; the line start, no longer needed
    ret

.none:
    ld      hl, 0
    ret

; -----------------------------------------------------------------------------
; MatchLabelValue: HL = the value of a name= line, i.e. `<filename>,<label>`.
;
;   out carry clear and HL -> the label when the filename is (NamePtr)'s
; -----------------------------------------------------------------------------
MatchLabelValue:
    ld      de, (NamePtr)
.cmp:
    IFDEF ROM_DATABASE_MENU
    push    hl
    ex      de, hl
    ld      a, (hl)
    cp      '['
    call    z, SkipMapperTag
    ex      de, hl
    pop     hl
    ld      a, (hl)
    cp      '['
    call    z, SkipMapperTag
    ENDIF
    ld      a, (de)
    or      a
    jr      z, .nameEnd
    ld      b, a

    ld      a, (hl)
    call    IsFieldEnd
    jr      z, .no                  ; the field ended first: a shorter name
    call    UpperCase
    ld      c, a
    ld      a, b
    call    UpperCase
    cp      c
    jr      nz, .no
    inc     hl
    inc     de
    jr      .cmp

.nameEnd:
    ; Spaces between the filename and the comma are allowed -- `name = X , Y` --
    ; but only there: a space inside the field is part of the filename, because
    ; FAT names may contain them.
    ld      a, (hl)
    cp      ' '
    jr      nz, .comma
    inc     hl
    jr      .nameEnd

.comma:
    cp      ','
    jr      nz, .no                 ; a name= line with no label says nothing

    inc     hl
.space:
    ld      a, (hl)
    cp      ' '
    jr      nz, .haveLabel
    inc     hl
    jr      .space

.haveLabel:
    or      a
    jr      z, .no                  ; nothing after the comma
    cp      13
    jr      z, .no
    cp      10
    jr      z, .no
    or      a                       ; clears carry, and A is not zero here
    ret

.no:
    scf
    ret

; IsFieldEnd: Z when A ends the filename field of a name= line.
IsFieldEnd:
    or      a
    ret     z
    cp      13
    ret     z
    cp      10
    ret     z
    cp      ','
    ret

; -----------------------------------------------------------------------------
; IsNameLine: Z when the line at HL is a name= line. HL is preserved, DE is not.
; -----------------------------------------------------------------------------
IsNameLine:
    push    hl
    ld      de, StrKeyName
    call    StrStartsNoCase
    jr      nz, .no

    ld      a, (hl)
    cp      '='
    jr      z, .yes
    cp      ' '
    jr      z, .yes
    cp      9
    jr      z, .yes

.no:
    pop     hl
    or      1                       ; NZ
    ret

.yes:
    pop     hl
    xor     a                       ; Z
    ret

; -----------------------------------------------------------------------------
; IsBootable: Z when the name at HL is one the menu should offer.
;   *.ROM or *.DSK, and not loader.rom, and not the configured Disk ROM.
; -----------------------------------------------------------------------------
IsBootable:
    push    hl
    ld      de, StrLoaderRom
    call    StrEqNoCase
    pop     hl
    jr      z, .no
    IFDEF AUTO_LOADER
    DEFINE HIDE_LITE_ROM
    ELSE
    ENDIF
    IFDEF HIDE_LITE_ROM
    push    hl
    ld      de, StrLiteRom
    call    StrEqNoCase
    pop     hl
    jr      z, .no
    ENDIF
    push    hl
    ld      de, StrDiskRom
    call    StrEqNoCase
    pop     hl
    jr      z, .no

    ; And whatever diskrom= names. That value may be a path, so the comparison
    ; is against its last component: a dump in /roms is the same file the
    ; browser is looking at when it lists /roms.
    ld      a, (DiskRomName)
    or      a
    jr      z, .checkExt

    push    hl
    ld      hl, DiskRomName
    call    BaseName
    ex      de, hl                  ; DE -> the dump's own name
    pop     hl
    push    hl
    call    StrEqNoCase
    pop     hl
    jr      z, .no

.checkExt:
    push    hl
    ld      de, StrExtRom
    call    HasExtension
    pop     hl
    ret     z

    ld      de, StrExtDsk
    call    HasExtension
    ret

.no:
    or      1                       ; NZ: not offered
    ret

; -----------------------------------------------------------------------------
; HasExtension: Z when the name at HL ends with the four characters at DE
; (".ROM" / ".DSK"), case-insensitively.
; -----------------------------------------------------------------------------
HasExtension:
    push    de
    call    StrLen                  ; B = length
    pop     de
    ld      a, b
    cp      5
    jr      c, .no                  ; "x.ROM" is the shortest that can match

    ; HL += length - 4
    ld      c, b
    ld      b, 0
    add     hl, bc
    dec     hl
    dec     hl
    dec     hl
    dec     hl

    ld      b, 4
.loop:
    ld      a, (de)
    ld      c, a
    ld      a, (hl)
    call    UpperCase
    cp      c
    jr      nz, .no
    inc     hl
    inc     de
    djnz    .loop
    xor     a                       ; Z: matched
    ret

.no:
    or      1
    ret

; -----------------------------------------------------------------------------
; SortEntries orders SortOrder, not the table: directories first, then files,
; each case-insensitively alphabetical. Moving one-byte indices instead of
; 34-byte records is what keeps an insertion sort affordable here -- 255 entries
; worst case is a few hundred thousand T-states, once per directory.
; -----------------------------------------------------------------------------
SortEntries:
    ; SortOrder = 0, 1, 2, ...
    ld      hl, SortOrder
    ld      bc, (EntryCount)
    ld      a, b
    or      c
    ret     z
    ld      e, 0
.fill:
    ld      (hl), e
    inc     hl
    inc     e
    dec     bc
    ld      a, b
    or      c
    jr      nz, .fill

    ; Insertion sort over the index array.
    ld      hl, 1
    ld      (SortI), hl
.outer:
    ld      hl, (SortI)
    ld      de, (EntryCount)
    call    CmpHLDE
    ret     nc                      ; done

    ld      (SortJ), hl
.inner:
    ld      hl, (SortJ)
    ld      a, h
    or      l
    jr      z, .nextOuter           ; reached the front

    ; Compare the entries SortOrder[j-1] and SortOrder[j] name, and swap the two
    ; indices while they are out of order.
    ld      de, SortOrder
    add     hl, de                  ; HL -> SortOrder[j]
    ld      (SortSlot), hl

    dec     hl
    ld      c, (hl)                 ; the index before it
    ld      b, 0
    call    EntryAddr
    ld      (LeftPtr), hl

    ld      hl, (SortSlot)
    ld      c, (hl)
    ld      b, 0
    call    EntryAddr
    ex      de, hl                  ; DE -> right entry
    ld      hl, (LeftPtr)           ; HL -> left entry
    call    EntryLess               ; carry set when right sorts before left
    jr      nc, .nextOuter          ; in order: this element has found its place

    ld      hl, (SortSlot)
    ld      a, (hl)
    dec     hl
    ld      c, (hl)
    ld      (hl), a
    inc     hl
    ld      (hl), c

    ld      hl, (SortJ)
    dec     hl
    ld      (SortJ), hl
    jr      .inner

.nextOuter:
    ld      hl, (SortI)
    inc     hl
    ld      (SortI), hl
    jr      .outer

; -----------------------------------------------------------------------------
; EntryLess: carry set when the entry at DE sorts before the entry at HL.
; Directories first, then case-insensitive name order.
; -----------------------------------------------------------------------------
EntryLess:
    ld      a, (de)
    ld      c, a                    ; C = right attribute
    ld      a, (hl)                 ; A = left attribute
    cp      c
    jr      z, .sameKind

    ; A higher attribute sorts first: the parent row (2), then directories (1),
    ; then files (0). "cp c" already set carry when the left attribute is the
    ; lower one, which is exactly "the right one sorts first", so there is
    ; nothing else to decide.
    ;
    ; This used to ask only whether the right attribute was non-zero, which
    ; cannot tell 1 from 2 -- so whether ".." came first depended on the order
    ; the insertion sort happened to compare things in.
    ret

.sameKind:
    push    hl
    push    de

    ; By what the row shows, not by the filename under it: a directory whose
    ; files all have labels would otherwise come out in an order with no visible
    ; rule to it.
    call    EntryDispStr            ; HL -> left display string
    ex      de, hl
    call    EntryDispStr            ; HL -> right display string
    ex      de, hl                  ; HL -> left, DE -> right
    call    StrCmpNoCaseLine       ; carry set when DE < HL

    pop     de
    pop     hl
    ret

.yes:
    scf
    ret

.no:
    or      a
    ret

; -----------------------------------------------------------------------------
; EntryAddr: HL = EntryTable + BC * ENTRY_SIZE.  ENTRY_SIZE is 36 = 32 + 4,
; which is the reason for the spare byte: five shifts and one add, no multiply.
; -----------------------------------------------------------------------------
EntryAddr:
    push    de
    ld      h, b
    ld      l, c
    add     hl, hl
    add     hl, hl                  ; HL = index * 4
    ld      d, h
    ld      e, l                    ; DE = index * 4
    add     hl, hl
    add     hl, hl
    add     hl, hl                  ; HL = index * 32
    add     hl, de                  ; HL = index * 36
    ld      de, EntryTable
    add     hl, de
    pop     de
    ret

; -----------------------------------------------------------------------------
; EntryDispStr: the string the entry at HL is listed under -- its label when the
; ini gave it one, its filename otherwise.
;
;   in/out  HL = entry / the string
; -----------------------------------------------------------------------------
EntryDispStr:
    push    de
    ld      de, ENTRY_DISP
    add     hl, de
    ld      e, (hl)
    inc     hl
    ld      d, (hl)
    ld      a, d
    or      e
    jr      nz, .label

    inc     hl                      ; ENTRY_NAME follows ENTRY_DISP
    pop     de
    ret

.label:
    ex      de, hl
    pop     de
    ret

; -----------------------------------------------------------------------------
; SortedEntryAddr: HL -> the entry drawn at position BC of the sorted list.
; -----------------------------------------------------------------------------
SortedEntryAddr:
    push    de
    ld      hl, SortOrder
    add     hl, bc
    ld      c, (hl)
    ld      b, 0
    pop     de
    jp      EntryAddr

; =============================================================================
; PATHS
; =============================================================================

; -----------------------------------------------------------------------------
; StartDirFromLast sets CurPath to the directory part of LastPath, or "/" when
; there is none. AutoBoot is raised only when LastPath actually names something.
; -----------------------------------------------------------------------------
StartDirFromLast:
    ld      hl, CurPath
    ld      (hl), '/'
    inc     hl
    ld      (hl), 0

    ld      a, (LastPath)
    or      a
    ret     z                       ; no last= : root, and no auto-boot

    ld      a, 1
    ld      (AutoBoot), a

    ; Copy LastPath, then cut it at its last '/'. A path with only the leading
    ; slash cuts to "/" itself.
    ld      hl, LastPath
    ld      de, CurPath
    call    StrCopy

    ld      hl, CurPath
    ld      d, 0
    ld      e, 0
.scan:
    ld      a, (hl)
    or      a
    jr      z, .cut
    cp      '/'
    jr      nz, .next
    ld      d, h
    ld      e, l
.next:
    inc     hl
    jr      .scan

.cut:
    ld      a, d
    or      e
    jr      z, .root
    ex      de, hl                  ; HL -> the last '/'
    ld      de, CurPath
    call    CmpHLDE
    jr      z, .keepRootSlash
    ld      (hl), 0
    ret

.keepRootSlash:
    inc     hl                      ; keep "/" itself
    ld      (hl), 0
    ret

.root:
    ld      hl, CurPath
    ld      (hl), '/'
    inc     hl
    ld      (hl), 0
    ret

; -----------------------------------------------------------------------------
; BuildFullPath: FullPath = CurPath + "/" + the selected entry's alias, with the
; root's own slash not doubled.
; -----------------------------------------------------------------------------
BuildFullPath:
    ld      hl, CurPath
    ld      de, FullPath
    call    StrCopy
    dec     de                      ; StrCopy leaves DE past the terminator; the
                                    ; name is written over the terminator itself

    ; Only add a separator if the path does not already end in one, or the root
    ; would come out as "//name".
    dec     de
    ld      a, (de)
    inc     de
    cp      '/'
    jr      z, .noSlash
    ld      a, '/'
    ld      (de), a
    inc     de

.noSlash:
    ld      bc, (SelIndex)
    call    SortedEntryAddr
    ld      bc, ENTRY_NAME
    add     hl, bc
    call    StrCopy
    ret

; -----------------------------------------------------------------------------
; AppendEntryToPath appends "/alias" of the selected entry to CurPath, unless
; that would overflow PATH_MAX, in which case it does nothing.
; -----------------------------------------------------------------------------
AppendEntryToPath:
    call    BuildFullPath
    ld      hl, FullPath
    call    StrLen                  ; B = length
    ld      a, b
    cp      PATH_MAX + 1
    ret     nc                      ; too long: stay where we are

    ld      hl, FullPath
    ld      de, CurPath
    call    StrCopy
    ret

; =============================================================================
; BOOT
; =============================================================================

; -----------------------------------------------------------------------------
; BootPath boots the path at HL: .dsk images go through CMD_MAP_DISK and a soft
; reset, Nextor kernels through CMD_MAP_NEXTOR and a soft reset, everything
; else through CMD_MAP_FILE and the cartridge's init vector.
;
; The ini is written first. Once the cartridge is swapped there is no ROM left
; to run a save from, and the point of the whole browser is that the choice
; survives the power cycle.
; -----------------------------------------------------------------------------
BootPath:
    ; Remember the choice, then persist it.
    ld      de, LastPath
    call    StrCopy

    ld      hl, LastPath
    call    IsNextorPath
    jp      z, BootNextorPath

    ld      hl, LastPath
    call    IsDskPath
    ld      a, 0
    jr      nz, .gotType
    inc     a
.gotType:
    ld      (SelectedType), a

    call    SaveIni

    ; Path into the buffer, then the Disk ROM name as a second string -- empty
    ; means the firmware's default -- and the second drive's image as a third,
    ; empty when there is none. CMD_MAP_FILE never looks past the first
    ; terminator, so a ROM boot is unaffected by either.
    ld      hl, LastPath
    ld      de, CART_BUF
    call    StrCopy
    ld      hl, DiskRomName
    call    StrCopy
    ld      hl, Disk2Path
    call    StrCopy

    call    PrepareMountTimeout

    ; Hand the machine over with a clean screen. Screen 5's VRAM is nothing like
    ; what a game expects to find, and leaving it there shows up as garbage
    ; character cells in anything that only initialises the parts of VRAM it
    ; uses -- which is most things. loader_fav.asm does exactly this, for
    ; exactly this reason.
    call    RestoreBootScreen

    ; Both commands swap the ROM this code is running from, so the rest happens
    ; in RAM.
    ld      hl, SwitchStubRom
    ld      de, SAFERAM
    ld      bc, SwitchStubEnd - SwitchStubRom
    ldir
    di
    jp      SAFERAM

; -----------------------------------------------------------------------------
; RestoreBootScreen leaves the machine in SCREEN 1 with the first 16 KB of VRAM
; cleared, which is what a cartridge or a disk boot expects to inherit.
; -----------------------------------------------------------------------------
RestoreBootScreen:
    ; Interrupts stay off for both calls, and this is what stops the striped
    ; screen that used to show up at the handover.
    ;
    ; CHGMOD leaves SCREEN 1 with the cursor enabled, and from then on the BIOS
    ; interrupt handler blinks it -- which means it writes VRAM, with its own
    ; address setup. FILVRM sets one address and then relies on auto-increment
    ; for 16 KB, so a blink landing inside that fill sends the rest of it to
    ; wherever the handler left the counter: a band cleared, a band not, a band
    ; overwritten. Which is what a mattress on screen is.
    ;
    ; They are not re-enabled: the next thing to happen is the cartridge being
    ; swapped out from under this code.
    di

    ld      a, 1
    call    CHGMOD

    IFDEF AUTO_LOADER
    DEFINE ATOMIC_BOOT_CLEAR
    ELSE
    ENDIF
    IFDEF ATOMIC_BOOT_CLEAR
    ; MSX1 CHGMOD/FILVRM can enable interrupts. Hide the BIOS cursor and
    ; perform an atomic, paced fill instead of calling FILVRM.
    di
    xor     a
    ld      ($FCA9), a
    ld      hl, 0
    call    SETWRT
    di
    ld      bc, $4000
.clearTextBoot:
    xor     a
    out     (VDP_DATA), a
    dec     bc
    ld      a, b
    or      c
    jr      nz, .clearTextBoot
    ELSE
    xor     a
    ld      hl, $0000
    ld      bc, $4000
    call    FILVRM
    ENDIF
    ret

; -----------------------------------------------------------------------------
; BaseName: HL -> whatever follows the last '/' in the path at HL, or the path
; itself when there is none.
; -----------------------------------------------------------------------------
BaseName:
    push    de
    ld      d, h
    ld      e, l                    ; the answer so far: the whole string

.scan:
    ld      a, (hl)
    or      a
    jr      z, .done
    inc     hl
    cp      '/'
    jr      nz, .scan
    ld      d, h
    ld      e, l                    ; one past this slash, so far the last one
    jr      .scan

.done:
    ex      de, hl
    pop     de
    ret

; -----------------------------------------------------------------------------
; IsDskPath: Z when the path at HL ends in ".DSK", case-insensitively.
; -----------------------------------------------------------------------------
IsDskPath:
    ld      de, StrExtDsk
    jp      HasExtension

; -----------------------------------------------------------------------------
; The switch stub. It runs from SAFERAM because the ROM it was copied from stops
; existing the moment the command succeeds, and it polls through MAILRAM -- the
; same RAM-resident, two-address poll everything else uses, which is why this
; stub does not carry a copy of one.
; -----------------------------------------------------------------------------
SwitchStubRom:
    DISP    SAFERAM

    ld      a, (SelectedType)
    or      a
    jr      nz, .diskPath

    ld      a, CMD_MAP_FILE
    call    MAILRAM
    or      a
    jr      nz, .failed

    ; Let the firmware finish swapping before the Z80 fetches from the new ROM.
    ld      bc, 500
.delay:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .delay

    ld      hl, ($4002)
    ei
    jp      (hl)

.diskPath:
    ld      a, CMD_MAP_DISK
    call    MAILRAM
    or      a
    jr      nz, .failed

    ld      bc, 500
.diskDelay:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .diskDelay

    di
    jp      0                       ; soft reset: the BIOS rescans and finds the
                                    ; disk interface, whose INIT only runs then

.failed:
    cp      MAIL_TIMEOUT
    jp      z, MountTimeoutRAM
    jp      MountFailed             ; failure: original ROM is still mapped

    ENT
SwitchStubEnd:
    ASSERT SwitchStubEnd-SwitchStubRom <= $80

; =============================================================================
; EXIT
; =============================================================================

ExitToBasic:
    IFDEF AUTO_LOADER
    call    PersistVolume
    ENDIF
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
    ldir

    ld      sp, (SavedSP)
    jp      SAFERAM

.noScc:
    ld      sp, (SavedSP)
    ei
    ret

SccExitStubRom:
    DISP    SAFERAM

    ld      a, CMD_MAP_SCC
    ld      (CART_CMD), a

    ; Match the favourites loader's working exit path: do not poll through the
    ; old loader after asking it to replace itself with the bare SCC cartridge.
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

; =============================================================================
; RENDERING
; =============================================================================

    IFDEF AUTO_LOADER
; Preserve the input character and dispatch state while restoring the footer.
DismissDiskStatus:
    push    af
    ld      a, (DiskStatusVisible)
    or      a
    jr      z, .done
    push    bc
    push    de
    push    hl
    call    DrawFooter
    pop     hl
    pop     de
    pop     bc
.done:
    pop     af
    ret
    ENDIF

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
    IFDEF AUTO_LOADER
    xor a
    ld (RbscProgress), a
    ld a, (HelpActive)
    cp 3
    jp z, CloseHelp
    ENDIF
    IFDEF AUTO_LOADER
    call    DismissVolume
    ENDIF
    ld      a, (HelpActive)
    IFDEF AUTO_LOADER
    cp      2
    jp      z, CloseHelp
    ELSE
    or      a
    jp      nz, CloseHelp
    ENDIF
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
    call    DrawList
    call    DrawFooter
    jp      PollLoop

; One NUL-terminated string per line; an extra NUL ends the help table.
    IFNDEF AUTO_LOADER
    INCLUDE "graphic_frame.inc"
    ENDIF

; -----------------------------------------------------------------------------
; ComputeEntryCap works out how many entries fit between the table and the stack
; that sits under HIMEM.
;
; HIMEM is where free RAM ends, and on a machine with a disk interface the Disk
; ROM has already taken everything above it for its own buffers. Assuming a
; fixed ceiling instead of reading this is what corrupted those buffers and
; dropped the machine into BASIC.
; -----------------------------------------------------------------------------
ComputeEntryCap:
    ld      hl, (HIMEM)
    ld      de, (SavedSP)
    or      a
    sbc     hl, de
    jr      c, .himemLower
    ex      de, hl                  ; SavedSP is the lower ceiling
    jr      .haveCeiling
.himemLower:
    add     hl, de                  ; restore HIMEM after the comparison
.haveCeiling:
    ld      de, STACK_RESERVE
    or      a
    sbc     hl, de                  ; leave the stack its room first
    jr      c, .none

    ld      de, EntryTable
    or      a
    sbc     hl, de                  ; bytes available for the table
    jr      c, .none
    jr      z, .none

    ; Divide by ENTRY_SIZE the only way a Z80 divides: by subtracting.
    ld      bc, 0
.count:
    ld      de, ENTRY_SIZE
    or      a
    sbc     hl, de
    jr      c, .haveCount
    inc     bc
    ld      a, b
    or      a
    jr      nz, .capped             ; already past 255, so past ENTRY_MAX
    ld      a, c
    cp      ENTRY_MAX
    jr      nc, .capped
    jr      .count

.haveCount:
    ld      (EntryCap), bc
    ret

.capped:
    ld      bc, ENTRY_MAX
    ld      (EntryCap), bc
    ret

.none:
    ld      bc, 0
    ld      (EntryCap), bc
    ret

; -----------------------------------------------------------------------------
; BuildGlyphTable precomputes the four VRAM bytes for every one of the 256 bit
; patterns a glyph line can hold.
;
; The shared renderer works out each pair of pixels as it goes: two reads and
; two writes of GlyphBits, two colour choices, a call and a return, about 137
; T-states per VRAM byte. That is most of the 1.8 ms a single character cell
; costs. With a table the inner loop is a fetch and an OUT.
;
; The UI table is fixed; the second table caches the most recent entry color.
; -----------------------------------------------------------------------------
    IFNDEF AUTO_LOADER
    INCLUDE "graphic_list.inc"
    ENDIF

ClearRowBuf:
    ld      hl, RowBuf
    ld      b, LIST_WIDTH
.fill:
    ld      (hl), ' '
    inc     hl
    djnz    .fill
    ld      (hl), 0
    ret

CopyToRowBuf:
    ld      a, (hl)
    or      a
    ret     z
    ld      (de), a
    inc     hl
    inc     de
    jr      CopyToRowBuf

; The same, but it stops after B characters: a name longer than its column is
; shown cut rather than written over the rest of the row. C is nonzero for
; INI labels, which stop at CR/LF; filenames stop only at NUL.
CopyToRowBufN:
    ld      a, (hl)
    IFDEF ROM_DATABASE_MENU
    cp      '['
    call    z, SkipMapperTag
    ENDIF
    or      a
    ret     z
    push    af
    ld      a, c
    or      a
    jr      z, .filename
    pop     af
    cp      13
    ret     z
    cp      10
    ret     z
    jr      .printable
.filename:
    pop     af
.printable:
    call    PrintableChar
    ld      (de), a
    inc     hl
    inc     de
    djnz    CopyToRowBufN
    ret

; Restrict display to the font's printable ASCII range; source bytes stay intact.
PrintableChar:
    cp      32
    jr      c, .replace
    cp      127
    ret     c
.replace:
    ld      a, '?'
    ret

; -----------------------------------------------------------------------------
; DrawList paints the whole window, row by row through DrawRow.
; -----------------------------------------------------------------------------
DrawList:
    ld      hl, (ScrollTop)
    ld      b, LIST_ROWS
.row:
    push    bc
    push    hl
    call    DrawRow
    pop     hl
    inc     hl
    pop     bc
    dec     b
    jr      nz, .row

    ; Every row is in normal colours; the selection goes on last.
    ld      hl, (EntryCount)
    ld      a, h
    or      l
    IFDEF AUTO_LOADER
    jp      z, DrawScrollHints
    ELSE
    ret     z
    ENDIF
    ld      hl, (SelIndex)
    call    HiliteRow
    jp      DrawScrollHints

; -----------------------------------------------------------------------------
; DrawCountdown shows the seconds left before last= boots by itself, and blanks
; the field once the countdown is off. Without it "the timer stopped" is
; something you have to take on trust; with it you can see it go.
;
; Redrawn only when the digit changes, so this costs one comparison on 49 frames
; out of 50.
; -----------------------------------------------------------------------------
    IFNDEF AUTO_LOADER
    INCLUDE "graphic_status.inc"
    ENDIF

ClearConfig:
    IFDEF ROM_DATABASE_MENU
    call    InitRomDatabase
    ENDIF
    ld      a, 1
    ld      (CfgMultidisk), a
    IFDEF AUTO_LOADER
    ld      a, 100
    ld      (CfgSccVolume), a
    xor     a
    ld      (VolumeFrames), a
    ld      (VolumeDirty), a
    ENDIF
    ; CurPath first: DrawFrame shows it before the ini has been read.
    ld      hl, CurPath
    ld      (hl), '/'
    inc     hl
    ld      (hl), 0

    xor     a
    ld      (DiskRomName), a
    ld      (NextorRomName), a
    ld      (NextorInvalid), a
    ld      (Disk2Path), a
    ld      (LastPath), a
    ld      (CfgSccOnExit), a
    ld      (CountdownStop), a
    ld      (AutoBoot), a
    ld      (SelectedType), a
    ld      (HelpActive), a
    ld      (HelpF1Held), a
    IFDEF AUTO_LOADER
    ld      (RbscProgress), a
    ld      (DiskStatusVisible), a
    ENDIF

    ld      a, $FF
    ld      (LastSecs), a           ; nothing drawn yet, so the first call draws

    ld      a, 15
    ld      (CfgColorDir), a
    ld      a, 10
    ld      (CfgColorRom), a
    ld      a, 7
    ld      (CfgColorDsk), a
    ld      hl, 5000
    ld      (CfgDelayMs), hl
    ld      (CfgDelay), hl
    ret

; -----------------------------------------------------------------------------
; ParseIni reads the buffer at HL line by line. Keys: last, disk2, delay,
; diskrom, scc_on_exit. Unknown lines, comments and section headers are skipped,
; and nothing is copied out -- SaveIni rewrites the file from what was parsed,
; which it can only do because this loader owns the file.
;
; name= lines are deliberately not parsed here. They stay in the buffer, where
; FindLabel reads them while directories are listed and PreserveNameLines copies
; them into the file being written.
; -----------------------------------------------------------------------------
ParseIni:
.nextLine:
    ld      a, (hl)
    or      a
    ret     z

    cp      ' '
    jp      z, .skipChar
    cp      9
    jp      z, .skipChar
    cp      13
    jp      z, .skipChar
    cp      10
    jp      z, .skipChar
    jp      .checkKey

.skipChar:
    inc     hl
    jp      .nextLine

.checkKey:
    IFDEF ROM_DATABASE_MENU
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
    ENDIF
    cp      ';'
    jp      z, .skipToEOL
    cp      '#'
    jp      z, .skipToEOL
    cp      '['
    jp      z, .skipToEOL

    ; Dispatch on the first letter, then on enough of the rest to disambiguate:
    ; last, disk2 / diskrom / delay, colors, scc_on_exit.
    cp      'm'
    jr      nz, .notMultidisk
    call    ParseMultidiskLine
    jp      .skipToEOL
.notMultidisk:
    cp      'c'
    jp      nz, .notColor
    call    ParseColorLine
    jp      .skipToEOL
.notColor:
    cp      'n'
    jp      z, .keyNextor
    cp      'l'
    jp      z, .keyLast
    cp      'd'
    jp      z, .keyD
    cp      's'
    jp      z, .keyScc
    jp      .skipToEOL

.keyNextor:
    call    ParseNextorLine
    jp      .skipToEOL

.keyLast:
    ld      de, LastPath
    ld      b, PATH_MAX
    jp      .copyValue

.keyD:
    inc     hl
    ld      a, (hl)
    dec     hl
    cp      'e'
    jp      z, .keyDelay
    cp      'i'
    jp      nz, .skipToEOL

    ; disk2 or diskrom: the fifth character tells them apart.
    push    hl
    inc     hl
    inc     hl
    inc     hl
    inc     hl
    ld      a, (hl)
    pop     hl
    cp      '2'
    jp      z, .keyDisk2
    ld      de, DiskRomName
    ld      b, DISKROM_NAME_MAX
    jp      .copyValue

.keyDisk2:
    ld      de, Disk2Path
    ld      b, PATH_MAX
    jp      .copyValue

.keyDelay:
    call    FindValue
    jp      c, .skipToEOL
    call    ParseNumber             ; DE = value, HL past it
    ld      (CfgDelayMs), de        ; milliseconds; Init makes the frame count
    jp      .skipToEOL

.keyScc:
    IFDEF AUTO_LOADER
    call    ParseSccVolumeLine
    jp      c, .skipToEOL
    ; A typo in the volume key must not become an scc_on_exit setting.
    push    hl
    ld      de, StrKeyScc
.sccKey:
    ld      a, (de)
    or      a
    jr      z, .sccKeyFound
    cp      (hl)
    jr      nz, .sccKeyOther
    inc     hl
    inc     de
    jr      .sccKey
.sccKeyOther:
    pop     hl
    jp      .skipToEOL
.sccKeyFound:
    pop     hl
    ENDIF
    call    FindValue
    jp      c, .skipToEOL
    call    ParseNumber
    ld      a, e
    or      d
    jp      z, .sccZero
    ld      a, 1
.sccZero:
    ld      (CfgSccOnExit), a
    jp      .skipToEOL

.copyValue:
    ; DE = destination, B = its capacity. HL is left past the value.
    push    de
    push    bc
    call    FindValue
    pop     bc
    pop     de
    jp      c, .skipToEOL
    call    CopyValue

.skipToEOL:
    ld      a, (hl)
    or      a
    ret     z
    inc     hl
    cp      13
    jp      z, .nextLine    ; jp: the key handlers above
    cp      10             ; are past jr's reach
    jp      z, .nextLine
    jp      .skipToEOL

; Parse only exact color keys and decimal values 0..15. Empty values keep defaults.
ParseColorLine:
    push    hl
    ld      de, StrKeyColorDir
    call    StrStartsNoCase
    pop     hl
    ld      bc, CfgColorDir
    jr      z, .value
    push    hl
    ld      de, StrKeyColorRom
    call    StrStartsNoCase
    pop     hl
    ld      bc, CfgColorRom
    jr      z, .value
    push    hl
    ld      de, StrKeyColorDsk
    call    StrStartsNoCase
    pop     hl
    ld      bc, CfgColorDsk
    ret     nz
.value:
    push    bc
    call    FindValue
    pop     bc
    ret     c
    ld      a, (hl)
    cp      '0'
    ret     c
    cp      '9' + 1
    ret     nc
    push    hl
    inc     hl
    ld      a, (hl)
    cp      '0'
    jr      c, .lengthOk
    cp      '9' + 1
    jr      nc, .lengthOk
    inc     hl
    ld      a, (hl)
    cp      '0'
    jr      c, .lengthOk
    cp      '9' + 1
    jr      nc, .lengthOk
    pop     hl
    ret
.lengthOk:
    pop     hl
    push    bc
    call    ParseNumber
    pop     bc
    ld      a, d
    or      a
    ret     nz
    ld      a, e
    cp      16
    ret     nc
    ld      (bc), a
    ret

; -----------------------------------------------------------------------------
; FindValue advances HL past the '=' and any spaces after it. Carry set when the
; line ends first, which is a key with no value.
; -----------------------------------------------------------------------------
FindValue:
.find:
    ld      a, (hl)
    or      a
    jr      z, .none
    cp      13
    jr      z, .none
    cp      10
    jr      z, .none
    cp      '='
    jr      z, .found
    inc     hl
    jr      .find

.found:
    inc     hl
.space:
    ld      a, (hl)
    cp      ' '
    jr      z, .skip
    cp      9
    jr      nz, .ok
.skip:
    inc     hl
    jr      .space

.ok:
    or      a                       ; clear carry
    ret

.none:
    scf
    ret

; -----------------------------------------------------------------------------
; CopyValue copies the value at HL to DE, at most B characters, stopping at the
; end of the line or at a space. HL is left on the terminator.
; -----------------------------------------------------------------------------
CopyValue:
.loop:
    ld      a, (hl)
    or      a
    jr      z, .done
    cp      13
    jr      z, .done
    cp      10
    jr      z, .done
    cp      ' '
    jr      z, .done
    cp      9
    jr      z, .done
    ld      (de), a
    inc     hl
    inc     de
    djnz    .loop

.done:
    xor     a
    ld      (de), a
    ret

; -----------------------------------------------------------------------------
; ParseNumber reads decimal digits at HL into DE.
; -----------------------------------------------------------------------------
ParseNumber:
    ld      de, 0
.loop:
    ld      a, (hl)
    sub     '0'
    ret     c
    cp      10
    ret     nc

    ; DE = DE * 10 + digit
    push    af
    push    hl
    ld      h, d
    ld      l, e
    add     hl, hl                  ; *2
    ld      d, h
    ld      e, l
    add     hl, hl                  ; *4
    add     hl, hl                  ; *8
    add     hl, de                  ; *10
    ld      d, h
    ld      e, l
    pop     hl
    pop     af

    ld      c, a
    ld      a, e
    add     a, c
    ld      e, a
    jr      nc, .noCarry
    inc     d
.noCarry:
    inc     hl
    jr      .loop

; -----------------------------------------------------------------------------
; SaveIni rewrites /loader.ini from the parsed values, then hands the write to a
; RAM stub. Canonical: known keys, no comments, and of the rest only the name=
; lines survive. That is only safe because this loader is the only one on the
; volume.
;
; The delay comes from CfgDelayMs, which nothing decrements. Writing the live
; counter instead -- which this did -- saved however much of the countdown was
; left when the entry was booted, and an expired one saved delay=0, so the next
; boot had no delay at all.
; -----------------------------------------------------------------------------
SaveIni:
    call    PreserveNameLines       ; DE = past the labels that survive

    ld      hl, StrKeyLast
    call    AppendStr
    ld      hl, LastPath
    call    AppendStr
    call    AppendEOL

    ld      a, (Disk2Path)
    or      a
    jr      z, .noDisk2
    ld      hl, StrKeyDisk2
    call    AppendStr
    ld      hl, Disk2Path
    call    AppendStr
    call    AppendEOL

.noDisk2:
    ld      hl, StrKeyDelay
    call    AppendStr
    ld      hl, (CfgDelayMs)        ; as configured, not as counted down
    call    AppendNumber
    call    AppendEOL

    ld      a, (DiskRomName)
    or      a
    jr      z, .noDiskRom
    ld      hl, StrKeyDiskRom
    call    AppendStr
    ld      hl, DiskRomName
    call    AppendStr
    call    AppendEOL

.noDiskRom:
    ld      a, (NextorRomName)
    or      a
    jr      z, .noNextor
    ld      hl, NextorKey
    call    AppendStr
    ld      hl, NextorRomName
    call    AppendStr
    call    AppendEOL
.noNextor:
    IFDEF ROM_DATABASE_MENU
    ld      a, (CfgKucOptions)
    or      a
    jr      z, .noKucOptions
    ld      hl, KucOptionsKey
    call    AppendStr
    ld      a, '1'
    ld      (de), a
    inc     de
    call    AppendEOL
.noKucOptions:
    ld      hl, RomDatabaseKey
    call    AppendStr
    ld      hl, RomDatabasePath
    call    AppendStr
    call    AppendEOL
    ENDIF
    ld      a, (CfgSccOnExit)
    or      a
    jr      z, .noScc
    ld      hl, StrKeyScc
    call    AppendStr
    ld      a, '1'
    ld      (de), a
    inc     de
    call    AppendEOL

.noScc:
    IFDEF AUTO_LOADER
    ld      hl, SccVolumeKey
    call    AppendStr
    ld      a, (CfgSccVolume)
    ld      l, a
    ld      h, 0
    call    AppendNumber
    call    AppendEOL
    ENDIF
    ld      hl, MultidiskKey
    call    AppendStr
    ld      a, (CfgMultidisk)
    add     a, '0'
    ld      (de), a
    inc     de
    call    AppendEOL
    ld      hl, StrKeyColorDir
    ld      a, (CfgColorDir)
    call    AppendColor
    ld      hl, StrKeyColorRom
    ld      a, (CfgColorRom)
    call    AppendColor
    ld      hl, StrKeyColorDsk
    ld      a, (CfgColorDsk)
    call    AppendColor
    ; Length for CMD_WRITE_BLOCK.
    ld      hl, RAM_INI_BUF
    ex      de, hl
    or      a
    sbc     hl, de                  ; HL = bytes written
    ld      (IniSize), hl

    ld      hl, SaveIniStubRom
    ld      de, SAFERAM
    ld      bc, SaveIniStubEnd - SaveIniStubRom
    ldir
    call    SAFERAM
    ret

AppendColor:
    push    af
    call    AppendStr
    pop     af
    ld      l, a
    ld      h, 0
    call    AppendNumber
    jp      AppendEOL

; -----------------------------------------------------------------------------
; PreserveNameLines compacts the ini buffer down to its name= lines, so SaveIni
; can append its own keys after them.
;
;   out DE = past the last line kept
;
; The labels are the one thing in the file this loader cannot regenerate, and
; rewriting the file canonically would delete them. Compaction is done in place,
; which is safe because lines are only ever dropped: the destination never
; overtakes the source.
;
; Init runs it too, as soon as ParseIni is done: from then on the buffer holds
; nothing but those lines, which is what keeps FindLabel's per-file scan short.
; Running it twice is harmless -- the second pass copies each line onto itself.
;
; Everything ENTRY_DISP points at is invalidated by a pass that actually moves
; something, which is why the second one runs at boot, after the last repaint.
; -----------------------------------------------------------------------------
PreserveNameLines:
    ld      hl, RAM_INI_BUF
    ld      de, RAM_INI_BUF

.nextLine:
    ld      a, (hl)
    or      a
    ret     z
    cp      13
    jr      z, .skipChar
    cp      10
    jr      z, .skipChar
    cp      ' '
    jr      z, .skipChar
    cp      9
    jr      z, .skipChar

    ; Leave room for the keys SaveIni still has to append, and stop keeping
    ; lines once it is gone.
    ld      a, d
    cp      HIGH (RAM_INI_BUF + INI_BUF_MAX - INI_KEYS_RESERVE)
    ret     nc

    push    de
    call    IsNameLine              ; clobbers DE, preserves HL
    pop     de
    jr      nz, .drop

.copy:
    ld      a, (hl)
    or      a
    jr      z, .copied
    cp      13
    jr      z, .copied
    cp      10
    jr      z, .copied

    ; The line-start check above leaves room for the keys; this one is what keeps
    ; a single very long line from walking out of the buffer entirely.
    ld      a, d
    cp      HIGH (RAM_INI_BUF + INI_BUF_MAX)
    jr      nc, .copied

    ld      a, (hl)
    ld      (de), a
    inc     hl
    inc     de
    jr      .copy

.copied:
    call    AppendEOL               ; the source terminator is left for .nextLine
    jp      .nextLine

.drop:
    ld      a, (hl)
    or      a
    ret     z
    inc     hl
    cp      13
    jp      z, .nextLine
    cp      10
    jp      z, .nextLine
    jr      .drop

.skipChar:
    inc     hl
    jp      .nextLine

AppendStr:
    ld      a, (hl)
    or      a
    ret     z
    ld      (de), a
    inc     hl
    inc     de
    jr      AppendStr

AppendEOL:
    ld      a, 13
    ld      (de), a
    inc     de
    ld      a, 10
    ld      (de), a
    inc     de
    ret

; -----------------------------------------------------------------------------
; AppendNumber writes HL as decimal at DE, without leading zeros.
; -----------------------------------------------------------------------------
AppendNumber:
    xor     a
    ld      (ZeroSeen), a
    ld      bc, 10000
    call    .digit
    ld      bc, 1000
    call    .digit
    ld      bc, 100
    call    .digit
    ld      bc, 10
    call    .digit
    ld      a, l
    add     a, '0'
    ld      (de), a
    inc     de
    ret

.digit:
    ld      a, 0
.sub:
    or      a
    sbc     hl, bc
    jr      c, .undo
    inc     a
    jr      .sub
.undo:
    add     hl, bc
    ; Leading zeros are dropped by never writing a zero digit before the first
    ; non-zero one; ZeroSeen says whether one has been written.
    ld      c, a
    or      a
    jr      nz, .write
    ld      a, (ZeroSeen)
    or      a
    ret     z
    xor     a
    ld      c, a

.write:
    ld      a, 1
    ld      (ZeroSeen), a
    ld      a, c
    add     a, '0'
    ld      (de), a
    inc     de
    ret

; -----------------------------------------------------------------------------
; The save stub. Like the switch stub it runs from RAM and polls through
; MAILRAM: writing the file erases and programs flash, so the cartridge answers
; nothing for tens of milliseconds.
; -----------------------------------------------------------------------------
SaveIniStubRom:
    DISP    SAFERAM

    ld      hl, StrIniFile
    ld      de, CART_BUF
.copyName:
    ld      a, (hl)
    ld      (de), a
    or      a
    jr      z, .named
    inc     hl
    inc     de
    jr      .copyName

.named:
    ld      a, CMD_OPEN_WRITE
    call    MAILRAM
    or      a
    ret     nz                      ; cannot write: boot anyway

    ld      hl, (IniSize)
    ld      a, l
    ld      (CART_BUF), a
    ld      a, h
    ld      (CART_BUF+1), a

    ld      hl, RAM_INI_BUF
    ld      de, CART_BUF+2
    ld      bc, (IniSize)
    ldir

    ld      a, CMD_WRITE_BLOCK
    call    MAILRAM

    ld      a, CMD_CLOSE
    call    MAILRAM
    ret

    ENT
SaveIniStubEnd:

; =============================================================================
; STRING HELPERS
; =============================================================================

; StrCopy copies the NUL-terminated string at HL to DE. Out: DE past the NUL.
StrCopy:
    ld      a, (hl)
    ld      (de), a
    or      a
    jr      z, .done
    inc     hl
    inc     de
    jr      StrCopy
.done:
    inc     de
    ret

; StrCopyN copies at most B characters from HL to DE and always terminates.
StrCopyN:
    ld      a, (hl)
    or      a
    jr      z, .term
    ld      (de), a
    inc     hl
    inc     de
    djnz    StrCopyN
.term:
    xor     a
    ld      (de), a
    ret

; StrEnd: HL -> the terminator of the string at HL.
StrEnd:
    ld      a, (hl)
    or      a
    ret     z
    inc     hl
    jr      StrEnd

; StrLen: B = length of the string at HL. HL is preserved.
StrLen:
    push    hl
    ld      b, 0
.loop:
    ld      a, (hl)
    or      a
    jr      z, .done
    inc     b
    inc     hl
    jr      .loop
.done:
    pop     hl
    ret

; StrEqNoCase: Z when the strings at HL and DE are equal, ignoring case.
StrEqNoCase:
    ld      a, (de)
    ld      c, a
    ld      a, (hl)
    call    UpperCase
    ld      b, a
    ld      a, c
    call    UpperCase
    cp      b
    ret     nz
    or      a
    ret     z                       ; both ended together
    inc     hl
    inc     de
    jr      StrEqNoCase

; StrStartsNoCase: Z when the string at HL starts with the one at DE, ignoring
; case. On a match HL is left past the prefix; otherwise HL is unchanged. BC is
; clobbered either way.
StrStartsNoCase:
    push    hl
.loop:
    ld      a, (de)
    or      a
    jr      z, .match
    ld      b, a
    ld      a, (hl)
    call    UpperCase
    ld      c, a
    ld      a, b
    call    UpperCase
    cp      c
    jr      nz, .no
    inc     hl
    inc     de
    jr      .loop

.match:
    pop     bc                      ; drop the saved HL: the caller wants the end
    xor     a
    ret

.no:
    pop     hl
    or      1
    ret

; StrCmpNoCaseLine: like StrCmpNoCase, but CR and LF end a string as well as
; NUL. Labels are pointers into the ini buffer and end where their line does.
StrCmpNoCaseLine:
    ld      a, (hl)
    call    LineChar
    ld      b, a
    ld      a, (de)
    call    LineChar
    ld      c, a
    cp      b
    jr      c, .less
    jr      nz, .notLess
    or      a
    jr      z, .notLess             ; both ended together
    inc     hl
    inc     de
    jr      StrCmpNoCaseLine

.less:
    scf
    ret

.notLess:
    or      a
    ret

; LineChar: A upper-cased, with CR and LF reported as the NUL they stand for.
LineChar:
    or      a
    ret     z
    cp      13
    jr      z, .end
    cp      10
    jr      z, .end
    jp      UpperCase

.end:
    xor     a
    ret

; StrCmpNoCase: carry set when the string at DE sorts before the one at HL.
StrCmpNoCase:
    ld      a, (hl)
    call    UpperCase
    ld      b, a
    ld      a, (de)
    call    UpperCase
    ld      c, a
    cp      b
    jr      c, .less
    jr      nz, .notLess
    ; Equal so far: the end of both means equal, which is not "less".
    or      a
    jr      z, .notLess
    inc     hl
    inc     de
    jr      StrCmpNoCase

.less:
    scf
    ret

.notLess:
    or      a
    ret

UpperCase:
    cp      'a'
    ret     c
    cp      'z' + 1
    ret     nc
    sub     $20
    ret

; CmpHLDE compares HL with DE without disturbing either: carry when HL < DE,
; Z when equal.
CmpHLDE:
    push    hl
    or      a
    sbc     hl, de
    pop     hl
    ret

; =============================================================================
; SHARED CODE
; =============================================================================

    INCLUDE "joystick.inc"
    INCLUDE "mailbox.inc"
    INCLUDE "disk_options.inc"
    IFDEF AUTO_LOADER
    INCLUDE "audio_options.inc"
    INCLUDE "audio_controls.inc"
    ENDIF
    DEFINE EXPLORER_TEXT
    IFNDEF AUTO_LOADER
    INCLUDE "vdp.inc"
    ENDIF
    IFDEF AUTO_LOADER
    INCLUDE "auto_ui.inc"
    INCLUDE "fw_help.inc"
    ENDIF

; =============================================================================
; VDP BOX DESCRIPTORS
; =============================================================================
; Colour and geometry are policy, so they stay with the loader rather than in
; vdp.inc: x, y, width, height, colour.

; The template for a one-row block move: SX, SY, DX, DY, NX, NY, then colour,
; argument and command -- fifteen bytes, written from R#32. Only SY and DY
; change, which ShiftList patches; the argument stays zero because no single
; move overlaps itself.
ShiftTemplate:
    dw      LIST_X, 0
    dw      LIST_X, 0
    dw      LIST_WIDTH * 8, LIST_PITCH
    db      0, 0, $D0               ; HMMM: the byte-oriented move

CmdShadow:
    dw      10, 38, 236, 152        ; shadow (X, Y, W, H)
    db      $11, 0, $C0             ; HMMV uses a packed pair of pixels

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
; DATA
; =============================================================================

; DX, DY, NX, NY, colour, argument, command -- the shape DrawBoxVDP sends from
; R#36. DY is patched per row; 11 is 4 xor 15, and $83 is a logical fill with
; the XOR operation.
    INCLUDE "nextor/nextor_menu.inc"

MOUNT_BROWSER EQU 1
    INCLUDE "mount_errors.inc"

HiliteTemplate:
    dw      LIST_X, 0
    dw      LIST_WIDTH * 8, 8
    db      HILITE_MASK, 0, $83

StrIniFile:     db "/loader.ini", 0
StrIniFileEnd:

StrLoaderRom:   db "loader.rom", 0
    IFDEF HIDE_LITE_ROM
StrLiteRom:     db "loader_lite.rom", 0
    ENDIF
StrDiskRom:     db "disk.rom", 0
StrExtRom:      db ".ROM"
StrExtDsk:      db ".DSK"

StrKeyColorDir: db "color_dir=", 0
StrKeyColorRom: db "color_rom=", 0
StrKeyColorDsk: db "color_dsk=", 0
StrKeyLast:     db "last=", 0
StrKeyDisk2:    db "disk2=", 0
StrKeyDelay:    db "delay=", 0
StrKeyDiskRom:  db "diskrom=", 0
StrKeyScc:      db "scc_on_exit=", 0
StrKeyName:     db "name", 0        ; matched as a prefix, so no '=' here

; The scroll arrows, eight lines each, bit 7 leftmost. They are drawn from here
; rather than from the font because the font has no arrows in it: what it holds
; at 0x5E, where a caret belongs, is a dither pattern.
PatArrowUp:     db $18, $3C, $7E, $FF, $18, $18, $18, $00
PatArrowDown:   db $18, $18, $18, $FF, $7E, $3C, $18, $00
PatBlank:       db $00, $00, $00, $00, $00, $00, $00, $00

MsgKeys:        db "[F1] Help * [RET] Open", 0
    IFDEF AUTO_LOADER
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
    ELSE
CreditsLines EQU HelpLines
    ENDIF
HelpLines:
    IFDEF AUTO_LOADER
    db "Help", 0
    db " ", 0
    db "[RET/SPACE] Open / Start", 0
    db "[ESC] Exit from menu", 0
    db "[INS/DEL] B: set / eject", 0
HelpVolumeLine:
    db "[+/-] SCC volume", 0
    db "[UP/DOWN] Select", 0
    db "[LEFT/RIGHT] Flip pages", 0
    db "[BS] Parent directory", 0
    db "[JOY A] Open / Start", 0
    db "[JOY B] Back / Exit", 0
    db "[F1] This help screen", 0
    db 0
    ELSE
    db "Help", 0
    db " ", 0
    db "[RET/SPACE] Open / boot", 0
    db "[INS] B: set [DEL] eject", 0
    db "[ESC] Exit to BASIC", 0
    db "[BS] Parent directory", 0
    db "[UP/DOWN] Select", 0
    db "[LEFT/RIGHT] Page", 0
    db "[F1] Help", 0
    db "[JOY A] Open / boot", 0
    db "[JOY B] Back / exit", 0
    IFDEF AUTO_LOADER
HelpVolumeLine:
    db "[+/-] SCC volume", 0
    ELSE
    db " ", 0
    ENDIF
    IFDEF AUTO_LOADER
    db "[ESC] Close", 0
    ELSE
    db "[ESC] / [F1] Close", 0
    ENDIF
    db 0
    ENDIF
MsgNoDisk2:     db "none", 0
MsgCapped:      db "TRUNCATED  ESC=exit", 0
MsgDirError:    db "CANNOT OPEN DIR", 0
MsgReading:     db "Reading...", 0
CornerTL:      db $1F, $3F, $60, $C0, $C0, $C0, $C0, $C0
CornerTR:      db $F8, $FC, $06, $03, $03, $03, $03, $03
CornerBL:      db $C0, $C0, $C0, $C0, $C0, $60, $3F, $1F
CornerBR:      db $03, $03, $03, $03, $03, $06, $FC, $F8
PatFolder:     db $00, $70, $7E, $42, $7E, $7E, $7E, $00
MsgDirMark:     db 1, " ", 0
MsgFileMark:    db "  ", 0
MsgEmpty:       db " ", 0

; PrintableChar replaces control codes; omit those 32 unused glyphs while
; retaining the renderer's Font8x8 + ASCII * 8 addressing.
Font8x8 EQU $-32*8
    IFNDEF AUTO_LOADER
    INCLUDE "loader_font.inc"
    ENDIF
Font8x8End:

    IFNDEF AUTO_LOADER
    INCLUDE "loader_title.inc"
    ENDIF

; =============================================================================
; ROM PADDING
; =============================================================================

    IFDEF AUTO_LOADER
    ; FindLabel uses this code while CMD_DIR_NEXT exposes the reply at
    ; 6000h..7FFFh. Keep BOTH the matcher and its dictionary in lower ROM.
    INCLUDE "mapper_tags.inc"
    INCLUDE "rom_database.inc"
    INCLUDE "kuc_options.inc"
    INCLUDE "startup_screen.inc"
LowerRomEnd:
    ASSERT $ <= $6000
    ORG $6000
; Startup only: directory replies are closed before this upper-ROM code runs.
; SortI/SortJ are free after sorting. Exact names win; a tag-free match must
; be unique, matching firmware resolution of pre-database last= paths.
RestoreLastSelection:
    ld hl, LastPath
    ld a, (hl)
    or a
    ret z
    ld hl, 0
    ld (SortI), hl
    dec hl
    ld (SortJ), hl                  ; FFFF=no candidate, FFFE=ambiguous
.scan:
    ld hl, (SortI)
    ld de, (EntryCount)
    call CmpHLDE
    jr nc, .finished
    ld b, h
    ld c, l
    call SortedEntryAddr
    ld a, (hl)
    or a
    jr nz, .next                   ; files only, never directories or '..'
    ld de, ENTRY_NAME
    add hl, de
    push hl
    ld hl, LastPath
    call BaseName
    ex de, hl
    pop hl
    push hl
    push de
    call StrCmpNoCase
    pop de
    pop hl
    jr z, .exact
    call MatchLastTagged
    jr nz, .next
    ld hl, (SortJ)
    inc hl
    ld a, h
    or l
    ld hl, $FFFE
    jr nz, .candidate
    ld hl, (SortI)
.candidate:
    ld (SortJ), hl
.next:
    ld hl, (SortI)
    inc hl
    ld (SortI), hl
    jr .scan
.finished:
    ld hl, (SortJ)
    ld de, $FFFE
    call CmpHLDE
    ret nc
    jr .select
.exact:
    ld hl, (SortI)
.select:
    ld (SelIndex), hl
    jp ClampScroll

; Case-insensitive filename comparison, skipping recognized mapper tags only.
MatchLastTagged:
    ld a, (hl)
    cp '['
    call z, SkipMapperTag
    ex de, hl
    ld a, (hl)
    cp '['
    call z, SkipMapperTag
    ex de, hl
    ld a, (hl)
    call UpperCase
    ld b, a
    ld a, (de)
    call UpperCase
    cp b
    ret nz
    or a
    ret z
    inc hl
    inc de
    jr MatchLastTagged

    INCLUDE "auto_renderers.inc"
RbscDrawBox EQU GraphicUI.DrawBoxVDP
RbscWaitVDP EQU GraphicUI.WaitVDP
RbscSetWrite EQU GraphicUI.SetVramWrite
    INCLUDE "rbsc_easter_egg.inc"
UpperRomEnd:
    ASSERT $ <= $7FFE
    SIZE $4000
    SAVEBIN "../generated/loader_auto.rom", $4000, $4000
    ELSE
    ASSERT  $ <= $6000
    SIZE    $2000
    SAVEBIN "../generated/loader.rom", $4000, $2000

    ENDIF
