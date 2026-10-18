; SCREEN 1: 32x24 full-width 8x8 cells on every MSX1 VDP.
; Patterns 0000, names 1800, colors 2000. Sprite areas remain untouched.
SETWRT          EQU $0053

InitTextScreen:
    ld      a, 1
    call    CHGMOD
    call    HideStartupScreen
    di
    xor     a
    ld      ($FCA9), a           ; disable BIOS cursor
    inc     a
    ld      ($F3DE), a           ; hide BASIC function-key labels
    ld      hl, TextRegisters
    ld      b, 3
    ld      c, $82
.reg:
    ld      a, (hl)
    out     (VDP_CTRL), a
    inc     hl
    ld      a, c
    out     (VDP_CTRL), a
    inc     c
    djnz    .reg
    ld      a, 6
    ld      ($F3E1), a
    ld      a, $80
    ld      ($F3E2), a
    xor     a
    ld      ($F3E3), a
    ld      hl, $0100           ; ASCII 32..127, unchanged graphical font
    call    TextSetWrite
    ld      hl, TextFont+32*8
    ld      bc, 96*8
.font:
    ld      a, (hl)
    out     (VDP_DATA), a
    inc     hl
    dec     bc
    ld      a, b
    or      c
    jr      nz, .font
    ; Codes 128..223 are the same glyphs inverted, for selected rows.
    ld      hl, TextFont+32*8
    ld      bc, 96*8
.inverse:
    ld      a, (hl)
    cpl
    out     (VDP_DATA), a
    inc     hl
    dec     bc
    ld      a, b
    or      c
    jr      nz, .inverse
    ld      hl, 8               ; custom frame tiles 1..12
    call    TextSetWrite
    ld      hl, TextFrameTiles
    ld      bc, 12*8
    call    TextCopyTiles
    ld      hl, 224*8
    call    TextSetWrite
    ld      hl, TextFolderTile
    ld      bc, 8
    call    TextCopyTiles
    IFDEF AUTO_LOADER
    ; Code 241 shares the green disk palette but keeps the diskette invariant
    ; when the rest of a selected row is inverted.
    ld      hl, 241*8
    call    TextSetWrite
    ld      hl, DiskBPattern
    ld      bc, 8
    call    TextCopyTiles
    ENDIF
    ; Private digits preserve normal filename colours in SCREEN 1.
    ld hl, 16*8
    call TextSetWrite
    ld hl, TextFont+'0'*8
    ld bc, 10*8
    call TextCopyTiles
    ld hl, 248*8
    call TextSetWrite
    ld hl, FooterStarPattern
    ld bc, 8
    call TextCopyTiles
    jp      TextColors
TextRegisters: db 6, $80, 0
TextSetWrite:
    call    SETWRT
    di                          ; BIOS SETWRT returns with interrupts enabled
    ret
TextCopyTiles:
    ld      a, (hl)
    out     (VDP_DATA), a
    inc     hl
    dec     bc
    ld      a, b
    or      c
    jr      nz, TextCopyTiles
    ret
TextColors:
    di
    ld      hl, $2000
    call    TextSetWrite
    ld      b, 32
    ld      a, $F4              ; white on dark blue
.colors:
    out     (VDP_DATA), a
    nop
    nop
    djnz    .colors
    ld      hl, $2000
    call    TextSetWrite
    ld      a, $74              ; cyan frame
    out     (VDP_DATA), a
    nop
    nop
    out     (VDP_DATA), a
    ld hl, $2002
    call TextSetWrite
    ld a, $84
    out (VDP_DATA), a
    nop
    nop
    out (VDP_DATA), a
    ld hl, $201F
    call TextSetWrite
    ld a, $74
    out (VDP_DATA), a
    ld      hl, $201C
    call    TextSetWrite
    ld      a, $A4              ; yellow folders (224..231)
    out     (VDP_DATA), a
    nop
    ld      a, $74              ; reserved group (232..239)
    out     (VDP_DATA), a
    nop
    ld      a, $34              ; green assigned disk (240..247)
    out     (VDP_DATA), a
    ret

; D=x, E=y -> BIOS SETWRT at 1800h + y*32+x; BC and DE preserved.
TextAddress:
    push    bc
    push    de
    ld      h, 0
    ld      l, e
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl
    ld      b, 0
    ld      c, d
    add     hl, bc
    ld      bc, $1800
    add     hl, bc
    call    SETWRT
    di                          ; SETWRT executes EI before RET on MSX BIOS
    pop     de
    pop     bc
    ret

; HL=text, B=width, D=x, E=y. Write/pad exactly B cells; caller owns EI.
; Every VRAM access runs with interrupts off. The loop leaves >12 T-states
; between data writes, including space padding, for the TMS9918 family.
DrawStringPadded:
    di
    push    hl
    call    TextAddress
    pop     hl
.character:
    ld      a, (hl)
    or      a
    jr      z, .space
    cp      13
    jr      z, .space
    cp      10
    jr      z, .space
    inc     hl
    call    PrintableChar
    jr      .write
.space:
    ld      a, ' '
.write:
    out     (VDP_DATA), a
    djnz    .character
    ret

DrawString8x8:
    ld      b, FOOTER_CHARS
    jp      DrawStringPadded

ClearTextScreen:
    di
    ld      de, 0
    call    TextAddress
    ld      bc, 768
.cell:
    ld      a, ' '
    out     (VDP_DATA), a
    dec     bc
    ld      a, b
    or      c
    jr      nz, .cell
    ret

; Trusted NUL-terminated frame strings, padded to B cells.
DrawTextTiles:
    di
    push    hl
    call    TextAddress
    pop     hl
.loop:
    ld      a, (hl)
    or      a
    jr      z, .pad
    inc     hl
    jr      .out
.pad:
    ld      a, ' '
.out:
    out     (VDP_DATA), a
    djnz    .loop
    ret

DrawFrame:
    call    TextColors
    call    ClearTextScreen
    ld      de, 0
    ld      hl, TextTop
    ld      b, 32
    call    DrawTextTiles
    ld      e, 2
    ld      hl, TextRule
    ld      b, 32
    call    DrawTextTiles
    ld      e, 20
    ld      hl, TextBottom
    ld      b, 32
    call    DrawTextTiles
    ld      e, 1
.sides:
    ld      d, 0
    ld      hl, TextSide
    ld      b, 1
    call    DrawTextTiles
    ld      d, 31
    ld      hl, TextRightSide
    ld      b, 1
    call    DrawTextTiles
    inc     e
    ld      a, e
    cp      2
    jr      nz, .nextSide
    inc     e
.nextSide:
    ld      a, e
    cp      20
    jr      nz, .sides
    ld      hl, TextTitle
    ld      d, 2
    ld      e, 1
    ld      b, 22
    call    DrawTextTiles
    jp      DrawPath

DrawPath:
    IFDEF FAV_TEXT
    ret
    ELSE
    ld      hl, CurPath
    call    StrLen
    ld      a, b
    cp      PATH_CHARS + 1
    jr      c, .short
    ld      c, b
    ld      b, 0
    add     hl, bc
    ld      bc, PATH_CHARS
    or      a
    sbc     hl, bc
.short:
    ld      d, PATH_X
    ld      e, PATH_Y
    ld      b, PATH_CHARS
    call    DrawStringPadded
    ei
    ret

    ENDIF

DrawHelp:
    di
    call    ReadFwVersion
    call    DrawFrame
    ld      a, (HelpActive)
    cp      2
    jr      z, .credits
    ld      hl, TextHelpTitle
    ld      d, 2
    ld      e, 1
    ld      b, 22
    call    DrawStringPadded
.credits:
    ld      hl, HelpLines
    ld      de, $0103
    ld      a, (HelpActive)
    cp      2
    jr      nz, .line
    ld      hl, CreditsLines
    ld      e, 6                    ; centre eleven rows inside the frame
.line:
    ld      a, (hl)
    or      a
    jr      z, .done
    push    hl
    push    de
    call    ResolveHelpLine
    ld      a, (HelpActive)
    cp      2
    jr      nz, .helpLine
    push    hl
    ld      b, 0
.width:
    ld      a, (hl)
    or      a
    jr      z, .centre
    inc     hl
    inc     b
    jr      .width
.centre:
    ld      a, 32
    sub     b
    srl     a
    ld      d, a
    pop     hl
    ld      a, 31
    sub     d
    ld      b, a                    ; clear only up to the inner right edge
    jr      .drawLine
.helpLine:
    ld      b, 30
.drawLine:
    call    DrawStringPadded
    pop     de
    pop     hl
.next:
    ld      a, (hl)
    inc     hl
    or      a
    jr      nz, .next
    inc     e
    jr      .line
.done:
    ld      a, (HelpActive)
    cp      2
    call    nz, DrawFwVersion
    ei
    ret

DrawFwVersion:
    ld      hl, FwHelpText
    ld      d, 15
    ld      e, 1
    ld      b, 16
    jp      DrawStringPadded
    IFDEF FAV_TEXT
TextTitle: db "OmniSCC loader", 0
    ELSE
TextTitle: db "OmniSCC explorer", 0
    ENDIF
TextHelpTitle: db "OmniSCC Help", 0
TextTop: db 1, 5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,2,0
TextRule: db 8,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,9,0
TextBottom: db 3,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,4,0
TextSide: db 6,0
TextRightSide: db 7,0
TextFrameTiles:
    db $00,$00,$0F,$10,$20,$20,$20,$20
    db $00,$00,$F0,$08,$04,$04,$04,$04
    db $20,$20,$20,$20,$10,$0F,$00,$00
    db $04,$04,$04,$04,$08,$F0,$00,$00
    db $00,$00,$FF,$00,$00,$00,$00,$00
    db $20,$20,$20,$20,$20,$20,$20,$20
    db $04,$04,$04,$04,$04,$04,$04,$04
    db $20,$20,$3F,$20,$20,$20,$20,$20
    db $04,$04,$FC,$04,$04,$04,$04,$04
    db $00,$10,$38,$7C,$10,$10,$00,$00
    db $00,$10,$10,$7C,$38,$10,$00,$00
    db $00,$00,$00,$00,$00,$FF,$00,$00
TextFolderTile: db $00,$70,$FE,$82,$82,$FE,$00,$00
    IFDEF FAV_TEXT
TextFont EQU Font8x8
    ELSE
TextFont EQU GraphicUI.Font8x8
    ENDIF

; CountStr is RAM; blank strings may be ROM and contain no digits to change.
DrawTimerString:
    push hl
.convert:
    ld a, (hl)
    or a
    jr z, .ready
    cp '0'
    jr c, .next
    cp '9'+1
    jr nc, .next
    sub '0'-16
    ld (hl), a
.next:
    inc hl
    jr .convert
.ready:
    pop hl
    jp DrawTextTiles
