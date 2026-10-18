; Hidden RBSC sequence. Called only for BIOS keyboard characters, never joystick
; input. A is preserved; carry is set only for a complete sequence in the menu.
FeedRbscKey:
    push af
    ld a, (UiText)
    or a
    jr nz, .ignore
    ld a, (HelpActive)
    or a
    jr nz, .ignore
    pop af
    push af
    push hl
    push de
    and $DF                        ; ASCII lower/upper case
    ld d, a
    ld a, (RbscProgress)
    ld e, a
    ld a, d
    ld d, 0
    ld hl, RbscSequence
    add hl, de
    cp (hl)
    jr nz, .restart
    inc e
    ld a, e
    cp 4
    jr z, .matched
    ld (RbscProgress), a
    jr .restore
.restart:
    cp 'R'                         ; repeated R starts a fresh attempt
    ld a, 0
    jr nz, .store
    inc a
.store:
    ld (RbscProgress), a
.restore:
    pop de
    pop hl
.ignore:
    pop af
    or a                           ; ordinary key continues through dispatch
    ret
.matched:
    xor a
    ld (RbscProgress), a
    pop de
    pop hl
    pop af
    scf
    ret
RbscSequence: db "RBSC"

OpenRbscLogo:
    call DismissVolume
    xor a
    ld (RbscProgress), a
    ld a, 3                        ; modal state, separate from Help and Credits
    ld (HelpActive), a
    ld (CountdownStop), a
    call DrawCountdown
    call KILBUF                    ; consume queued repeats before displaying
    call DrawRbscLogo
    jp PollLoop

; Native SCREEN 5 decoder. The existing modal frame masks the directory;
; CloseHelp rebuilds the unchanged menu and selection when any key is pressed.
DrawRbscLogo:
    di
    ld hl, CmdHelpBorder
    call RbscDrawBox
    ld hl, CmdHelpFill
    call RbscDrawBox
    call RbscWaitVDP
    ld hl, RbscBitmap
    ld de, 45*128+64/2
    ld c, RBSC_HEIGHT
.row:
    ex de, hl
    call RbscSetWrite
    ex de, hl
    push bc
    ld c, RBSC_WIDTH/2
.run:
    ld a, (hl)
    inc hl
    ld b, a
    and $0F
    push hl
    push de
    ld e, a
    ld d, 0
    ld hl, RbscPairs
    add hl, de
    ld a, b
    rrca
    rrca
    rrca
    rrca
    and $0F
    inc a
    ld b, a
    ld a, (hl)
    pop de
    pop hl
.pixels:
    out (VDP_DATA), a
    dec c
    djnz .pixels
    ld a, c
    or a
    jr nz, .run
    pop bc
    ld a, e
    add a, 128
    ld e, a
    jr nc, .noCarry
    inc d
.noCarry:
    dec c
    jr nz, .row
    ei
    ret
RbscPairs: db $44,$4F,$4A,$F4,$FF,$FA,$A4,$AF,$AA
    INCLUDE "rbsc_logo.inc"
