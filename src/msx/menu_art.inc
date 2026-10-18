; Shared browser/launcher help text and Screen 5 title rendering.
; DrawHelpLine requires interrupts disabled by its caller.

; Bracketed key names are yellow; all other help text is white.
; The markup lives in HelpLines so new bindings need only one table edit.
DrawHelpLine:
    ld      a, d
    ld      (TextX), a
    ld      a, e
    ld      (TextY), a
    ld      a, COLOR_TEXT
    ld      (TextColor), a
    ld      b, 24
.char:
    ld      a, (hl)
    or      a
    ret     z
    cp      '['
    jr      nz, .draw
    ld      a, COLOR_FOOTER
    ld      (TextColor), a
    ld      a, (hl)
.draw:
    push    hl
    push    bc
    push    af
    call    DrawChar8x8
    pop     af
    pop     bc
    pop     hl
    cp      ']'
    jr      nz, .next
    ld      a, COLOR_TEXT
    ld      (TextColor), a
.next:
    inc     hl
    ld      a, (TextX)
    add     a, 8
    ld      (TextX), a
    djnz    .char
    ret

; Native Screen 5 bitmap: each byte is (run-1, 2-bit palette pair).
; Exactly TITLE_WIDTH/2 VRAM bytes per row, no row terminator needed.
DrawTitle:
    ld      de, TITLE_Y * 128 + TITLE_X / 2
    call    DrawTitleAt
    ei
    ret

; DE is a byte address in any Screen 5 page. Preserve DI for startup drawing.
DrawTitleAt:
    di
    ld      hl, TitleBitmap
    ld      b, TITLE_WIDTH / 2
    ld      c, TITLE_HEIGHT
    jr      DrawBitmapRows

    IFDEF ROM_DATABASE_MENU
DrawStartupLogoAt:
    di
    ld      hl, StartupLogoBitmap
    ld      b, STARTUP_LOGO_WIDTH / 2
    ld      c, STARTUP_LOGO_HEIGHT
    ENDIF

; HL=RLE data, B=row width in bytes, C=height, DE=Screen 5 byte address.
DrawBitmapRows:
.row:
    ex      de, hl
    call    SetVramWrite
    ex      de, hl
    push    bc
    ld      c, b
.run:
    ld      a, (hl)
    inc     hl
    ld      b, a
    and     $0F
    push    de
    push    hl
    ld      e, a
    ld      d, 0
    ld      hl, TitlePairs
    add     hl, de
    ld      a, b
    rrca
    rrca
    rrca
    rrca
    and     $0F
    inc     a
    ld      b, a
    ld      a, (hl)
    pop     hl
    pop     de
.pixels:
    out     (VDP_DATA), a
    dec     c
    djnz    .pixels
    ld      a, c
    or      a
    jr      nz, .run
    pop     bc
    ld      a, e
    add     a, 128
    ld      e, a
    jr      nc, .noCarry
    inc     d
.noCarry:
    dec     c
    jr      nz, .row
    ret
TitlePairs:
    db $44, $4F, $47, $4A, $F4, $FF, $F7, $FA
    db $74, $7F, $77, $7A, $A4, $AF, $A7, $AA

    IFDEF ROM_DATABASE_MENU
; Draw footer keys in one pass: never paint an ASCII asterisk underneath
; the cyan star, since that intermediate glyph is visible during navigation.
DrawFooterKeys:
    ld a, d
    ld (TextX), a
    ld a, e
    ld (TextY), a
.next:
    ld a, (hl)
    or a
    ret z
    push hl
    cp '*'
    jr nz, .ordinary
    ld a, 7
    ld (TextColor), a
    ld de, FooterStarPattern
    call DrawGlyph8x8
    ld a, COLOR_FOOTER
    ld (TextColor), a
    jr .advance
.ordinary:
    call DrawChar8x8
.advance:
    pop hl
    inc hl
    ld a, (TextX)
    add a, 8
    ld (TextX), a
    jr .next
    ENDIF
