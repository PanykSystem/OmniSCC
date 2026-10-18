; Fast favourites text: expand bit patterns once, stream a scanline at a time.
; Callers keep interrupts disabled, as for vdp.inc. All four type/selection palettes
; are prepared at startup; moving the cursor never rebuilds the tables.
BuildFavGlyphTables:
    ld      a, (UiText)
    or      a
    ret     nz
    ld      a, 4
    ld      (FavBuildBg), a
    ld      a, (CfgColorRom)
    ld      hl, FavGlyphTable
    call    BuildFavTable
    ld      a, (CfgColorRom)
    ld      (FavBuildBg), a
    ld      a, 4
    ld      hl, FavGlyphSel
    call    BuildFavTable
    ld      a, COLOR_BG
    ld      (FavBuildBg), a
    ld      a, (CfgColorDsk)
    ld      hl, FavGlyphDsk
    call    BuildFavTable
    ld      a, (CfgColorDsk)
    ld      (FavBuildBg), a
    ld      a, COLOR_BG
    ld      hl, FavGlyphDskSel
    jp      BuildFavTable

DrawFavStringPadded:
    ld      a, b
    ld      (FavBandCount), a
    push    de
    ld      de, FavBandText
.copy:
    ld      a, (hl)
    cp      '['
    call    z, SkipMapperTag
    or      a
    jr      z, .space
    cp      13
    jr      z, .space
    cp      10
    jr      z, .space
    cp      ','
    jr      z, .space
    inc     hl
    jr      .store
.space:
    ld      a, ' '
.store:
    ld      (de), a
    inc     de
    djnz    .copy
    pop     de
    ; The row caller selected a palette by real file type and selection state.
    ld      hl, FavBandText
    ld      a, (FavBandCount)
    ld      b, a
    jp      FavDrawBand

BuildFavTable:
    ld      (FavBuildFg), a
    ld      c, 0                    ; C = the bit pattern being expanded
.pattern:
    ld      b, 4                    ; four VRAM bytes per pattern
    ld      e, c
.byteLoop:
    ; Two pixels per byte, high nibble first.
    ld      a, e
    rlca
    ld      e, a
    ld      a, (FavBuildBg)
    jr      nc, .haveFirst
    ld      a, (FavBuildFg)
.haveFirst:
    add     a, a
    add     a, a
    add     a, a
    add     a, a
    ld      d, a

    ld      a, e
    rlca
    ld      e, a
    ld      a, (FavBuildBg)
    jr      nc, .haveSecond
    ld      a, (FavBuildFg)
.haveSecond:
    or      d
    ld      (hl), a
    inc     hl
    djnz    .byteLoop

    inc     c
    jr      nz, .pattern
    ret

FavDrawBand:
    ld      (FavBandTextPtr), hl
    ld      a, b
    ld      (FavBandCount), a
    call    FavBandVramAddr

    xor     a
    ld      (FavBandLine), a

.lineLoop:
    di
    ld      hl, (FavBandAddr)
    call    SetVramWrite

    ld      hl, (FavBandTextPtr)
    ld      a, (FavBandCount)
    ld      b, a

.charLoop:
    push    bc
    push    hl

    ; This character's glyph line: Font8x8 + char * 8 + line.
    ld      a, (hl)
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    add     hl, hl
    ld      de, Font8x8
    add     hl, de
    ld      a, (FavBandLine)
    ld      e, a
    ld      d, 0
    add     hl, de
    ld      a, (hl)

    ; Its four VRAM bytes: GlyphTable + pattern * 4.
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    ld      de, (FavBandTable)
    add     hl, de

    ld      a, (hl)
    out     (VDP_DATA), a
    inc     hl
    ld      a, (hl)
    out     (VDP_DATA), a
    inc     hl
    ld      a, (hl)
    out     (VDP_DATA), a
    inc     hl
    ld      a, (hl)
    out     (VDP_DATA), a

    pop     hl
    inc     hl
    pop     bc
    djnz    .charLoop

    ld      hl, (FavBandAddr)
    ld      bc, 128                 ; one pixel line
    add     hl, bc
    ld      (FavBandAddr), hl

    ld      a, (FavBandLine)
    inc     a
    ld      (FavBandLine), a
    cp      8
    jr      c, .lineLoop
    ret

; -----------------------------------------------------------------------------
; FavBandVramAddr sets FavBandAddr from D = X (even) and E = Y: Y * 128 + X / 2, two
; pixels to the byte.
; -----------------------------------------------------------------------------
FavBandVramAddr:
    ld      l, e
    ld      h, 0
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl
    ld      a, d
    srl     a
    ld      c, a
    ld      b, 0
    add     hl, bc
    ld      (FavBandAddr), hl
    ret
