; Shared interface with the bitmap browser; glyph caches are unnecessary.
BuildGlyphTable:
BuildOneTable:
    ret

RowY:
    push    hl
    ld      de, (ScrollTop)
    or      a
    sbc     hl, de
    jr      c, .outside
    ld      de, LIST_ROWS
    call    CmpHLDE
    jr      nc, .outside
    ld      a, l
    add     a, LIST_Y0
    pop     hl
    or      a
    ret
.outside:
    pop     hl
    scf
    ret

DrawRow:
    call    RowY
    ret     c
    ld      (CurY), a
    push    hl
    ld      de, (EntryCount)
    call    CmpHLDE
    jr      nc, .blank
    call    ClearRowBuf
    pop     hl
    push    hl
    ld      b, h
    ld      c, l
    call    SortedEntryAddr
    push    hl
    ld      a, (hl)
    or      a
    jr      z, .file
    ld      a, 224
    ld      (RowBuf+1), a
    jr      .iconReady
.file:
    ; Ordinary ROM/DSK files have no icon, matching the graphical browser.
.iconReady:
    pop     hl
    call    IsDisk2Entry
    jr      nz, .notDisk2
    ld      a, 241
    ld      (RowBuf+1), a        ; replace the type icon; keep the following space
.notDisk2:
    call    EntryDispStr
    ld      c, a
    ld      de, RowBuf+3
    ld      b, LIST_WIDTH-3
    call    CopyToRowBufN
    pop     hl
    push    hl
    ld      de, (SelIndex)
    call    CmpHLDE
    jr      nz, .paint
    ; Invert printable glyphs, preserving folder and assigned-disk icons.
    ld      hl, RowBuf
    ld      b, LIST_WIDTH
.invert:
    ld      a, (hl)
    cp      128
    jr      nc, .keep
    add     a, 96
    ld      (hl), a
.keep:
    inc     hl
    djnz    .invert
    jr      .paint
.blank:
    call    ClearRowBuf
.paint:
    ld      hl, RowBuf
    ld      d, LIST_X
    ld      a, (CurY)
    ld      e, a
    ld      b, LIST_WIDTH
    call    DrawTextTiles
    ei
    pop     hl
    ret

; DrawRow already derives inverse glyphs from SelIndex. No XOR state to maintain.
HiliteRow:
    ret

DrawScrollHints:
    ld      hl, (ScrollTop)
    ld      a, h
    or      l
    ld      hl, TextBlank
    jr      z, .up
    ld      hl, TextUp
.up:
    ld      d, HINT_X
    ld      e, LIST_Y0
    ld      b, 1
    call    DrawTextTiles
    ld      hl, (ScrollTop)
    ld      de, LIST_ROWS
    add     hl, de
    ld      de, (EntryCount)
    call    CmpHLDE
    ld      hl, TextBlank
    jr      nc, .down
    ld      hl, TextDown
.down:
    ld      d, HINT_X
    ld      e, LIST_Y0+LIST_ROWS-1
    ld      b, 1
    call    DrawTextTiles
    ei
    ret
TextUp: db 10, 0
TextDown: db 11, 0
TextBlank: db 0
