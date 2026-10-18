; Graphical browser presentation; also included in the upper universal ROM.
BuildGlyphTable:
    ld      hl, GlyphTable
    ld      a, COLOR_TEXT
    call    BuildOneTable
    ld      hl, GlyphTableDir
    ld      a, (CfgColorRom)
    ld      (RowColor), a
    ; fall through

BuildOneTable:
    ld      (BuildFg), a
    ld      c, 0                    ; C = the bit pattern being expanded
.pattern:
    ld      b, 4                    ; four VRAM bytes per pattern
    ld      e, c
.byteLoop:
    ; Two pixels per byte, high nibble first.
    ld      a, e
    rlca
    ld      e, a
    ld      a, COLOR_BG
    jr      nc, .haveFirst
    ld      a, (BuildFg)
.haveFirst:
    add     a, a
    add     a, a
    add     a, a
    add     a, a
    ld      d, a

    ld      a, e
    rlca
    ld      e, a
    ld      a, COLOR_BG
    jr      nc, .haveSecond
    ld      a, (BuildFg)
.haveSecond:
    or      d
    ld      (hl), a
    inc     hl
    djnz    .byteLoop

    inc     c
    jr      nz, .pattern
    ret

; -----------------------------------------------------------------------------
; DrawBand draws B characters of the string at HL as one eight-line band.
;   in  HL = text, D = X (even), E = Y, B = characters
;
; The point is the loop order: one VRAM address per pixel line rather than one
; per character, so the VDP\'s auto-increment carries across the whole row. That
; turns 8 address setups per character into 8 per row -- 26 times fewer -- and
; with the glyph table the rest is a fetch and an OUT per byte. A row costs
; about 9 ms instead of 47.
; -----------------------------------------------------------------------------
DrawBand:
    ld      (BandText), hl
    ld      a, b
    ld      (BandCount), a
    call    BandVramAddr

    xor     a
    ld      (BandLine), a

.lineLoop:
    di
    ld      hl, (BandAddr)
    call    SetVramWrite

    ld      hl, (BandText)
    ld      a, (BandCount)
    ld      b, a

.charLoop:
    push    bc
    push    hl

    ; This character\'s glyph line: Font8x8 + char * 8 + line.
    ld      a, (hl)
    cp      1
    jr      z, .folder
    IFDEF AUTO_LOADER
    cp      2
    jr      z, .disk
    ENDIF
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    add     hl, hl
    ld      de, Font8x8
    add     hl, de
    jr      .glyph
    IFDEF AUTO_LOADER
.disk:
    ld      hl, DiskBPattern
    jr      .glyph
    ENDIF
.folder:
    ld      hl, PatFolder
.glyph:
    ld      a, (BandLine)
    ld      e, a
    ld      d, 0
    add     hl, de
    ld      a, (hl)

    ; Its four VRAM bytes: GlyphTable + pattern * 4.
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    ld      de, (BandTable)
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

    ei

    ld      hl, (BandAddr)
    ld      bc, 128                 ; one pixel line
    add     hl, bc
    ld      (BandAddr), hl

    ld      a, (BandLine)
    inc     a
    ld      (BandLine), a
    cp      8
    jr      c, .lineLoop
    ret

; -----------------------------------------------------------------------------
; BandVramAddr sets BandAddr from D = X (even) and E = Y: Y * 128 + X / 2, two
; pixels to the byte.
; -----------------------------------------------------------------------------
BandVramAddr:
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
    ld      (BandAddr), hl
    ret

; -----------------------------------------------------------------------------
; DrawPattern paints one eight-line glyph from the pattern bytes at HL instead
; of from the font, expanded through the same glyph table.
;
;   in  HL = eight pattern bytes, D = X (even), E = Y
;
; The scroll arrows need it. The bundled font has no arrow glyphs at all, and
; its 0x5E is a dither pattern rather than a caret -- which is exactly what the
; up arrow came out as on screen.
; -----------------------------------------------------------------------------
DrawPattern:
    push    hl
    call    BandVramAddr
    pop     hl

    ld      c, 8                    ; pixel lines left
.lineLoop:
    di
    push    hl
    ld      hl, (BandAddr)
    call    SetVramWrite
    pop     hl

    ld      a, (hl)
    inc     hl
    push    hl

    ; Its four VRAM bytes: BandTable + pattern * 4, as in DrawBand.
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    ld      de, (BandTable)
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
    ei

    ld      hl, (BandAddr)
    ld      de, 128                 ; one pixel line
    add     hl, de
    ld      (BandAddr), hl

    pop     hl
    dec     c
    jr      nz, .lineLoop
    ret

; -----------------------------------------------------------------------------
; HiliteRow flips the highlight on the row at list position HL.
;
; The row holds only two colours, 4 for the background and 15 for the text, and
; 4 xor 11 = 15 while 15 xor 11 = 4. So one LMMV fill with the logical XOR
; operation and a mask of 11 swaps them across the row: the highlight appears or
; disappears without a single character being redrawn. At roughly a millisecond
; per row that is the difference between a cursor that moves and one that crawls
; -- the glyph renderer costs about 1.8 ms per character cell.
;
; Being its own inverse is what lets MoveTo flip the old row off and the new row
; on without knowing which is which.
; -----------------------------------------------------------------------------
HiliteRow:
    call    RowY
    ret     c                       ; not inside the window
    ld      (HiliteCmd + 2), a      ; DY low byte
    xor     a
    ld      (HiliteCmd + 3), a      ; DY high byte

    di
    ld      hl, HiliteCmd
    call    DrawBoxVDP
    call    WaitVDP
    ei
    ret

; -----------------------------------------------------------------------------
; ShiftList moves the window one row with a single VDP block move.
;   in  A = 0: contents move up (the window scrolled down), last row vacated
;       A = 1: contents move down (the window scrolled up), first row vacated
;
; The vacated row still holds the old pixels; the caller draws over it. HMMM is
; the byte-oriented move, so the whole window costs about 20 ms against the half
; second that repainting eleven rows would take.
; -----------------------------------------------------------------------------
ShiftList:
    ; One row per block move, in an order where no move's source overlaps its
    ; own destination -- so the engine's direction bits do not matter and this
    ; does not rest on which bit of the argument byte is DIY. Guessing that bit
    ; wrong is what corrupted the screen when the list scrolled up: the copy ran
    ; forward over its own source and smeared one row down the window.
    ;
    ; Eleven moves of a single row cost what one move of eleven rows costs; the
    ; engine is paid by the byte.
    or      a
    jr      nz, .downward

    ; Contents move up: row 1 goes to row 0, row 2 to row 1, and so on. Each
    ; source is still untouched when it is read.
    ld      b, LIST_ROWS - 1
    ld      hl, LIST_Y0             ; destination of the first move
.upLoop:
    push    bc
    ld      a, l
    ld      (ShiftCmd + 6), a       ; DY low; DX is fixed in the template
    ld      a, h
    ld      (ShiftCmd + 7), a
    ld      bc, LIST_PITCH
    add     hl, bc
    ld      a, l
    ld      (ShiftCmd + 2), a       ; SY = DY + one row
    ld      a, h
    ld      (ShiftCmd + 3), a
    call    SendShift
    pop     bc
    djnz    .upLoop
    ret

.downward:
    ; Contents move down: row 10 goes to row 11 first, then 9 to 10, so again
    ; nothing is read after being written.
    ld      b, LIST_ROWS - 1
    ld      hl, LIST_Y0 + (LIST_ROWS - 2) * LIST_PITCH   ; last source row
.downLoop:
    push    bc
    ld      a, l
    ld      (ShiftCmd + 2), a       ; SY
    ld      a, h
    ld      (ShiftCmd + 3), a
    ld      bc, LIST_PITCH
    add     hl, bc
    ld      a, l
    ld      (ShiftCmd + 6), a       ; DY = SY + one row
    ld      a, h
    ld      (ShiftCmd + 7), a
    ld      bc, -2 * LIST_PITCH
    add     hl, bc                  ; next source, one row up
    call    SendShift
    pop     bc
    djnz    .downLoop
    ret

; -----------------------------------------------------------------------------
; SendShift hands ShiftCmd to the VDP: fifteen bytes from R#32, because a block
; move needs a source rectangle, which the fills written from R#36 do not have.
; -----------------------------------------------------------------------------
SendShift:
    ; HL and BC belong to the caller's loop, and otir walks HL to the end of the
    ; block while counting B down. Losing HL here is what made every move after
    ; the first one aim at ShiftCmd's own address instead of a screen row: one
    ; row shifted and the rest landed as scattered dots.
    push    hl
    push    bc

    di
    call    WaitVDP

    ld      a, 32
    out     (VDP_CTRL), a
    ld      a, $91
    out     (VDP_CTRL), a
    ld      c, VDP_INDIRECT
    ld      b, 15
    ld      hl, ShiftCmd
    otir

    call    WaitVDP
    ei

    pop     bc
    pop     hl
    ret

; -----------------------------------------------------------------------------
; DrawRow paints one row: the entry at list position HL, or a blank row when the
; position is past the end of the list. Positions outside the visible window are
; ignored, so callers need not check.
;
; This is the unit both the full redraw and a cursor step go through, which is
; what keeps a moved cursor looking exactly like a redrawn list.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; RowY turns a list position in HL into the screen Y of its row.
;   out  A = Y with carry clear; carry set when the position is not in the
;        window. HL is preserved.
; -----------------------------------------------------------------------------
RowY:
    push    hl
    ld      de, (ScrollTop)
    or      a
    sbc     hl, de
    jr      c, .outside
    ld      de, LIST_ROWS
    call    CmpHLDE
    jr      nc, .outside

    ; Y = LIST_Y0 + row * LIST_PITCH. L is 0..LIST_ROWS-1 here, and the loop is
    ; skipped for row 0 rather than letting djnz wrap 256 times.
    ld      a, LIST_Y0
    ld      b, l
    inc     b
    dec     b
    jr      z, .done
.addPitch:
    add     a, LIST_PITCH
    djnz    .addPitch
.done:
    pop     hl
    or      a                       ; carry clear: visible
    ret

.outside:
    pop     hl
    scf
    ret

; -----------------------------------------------------------------------------
; DrawRow paints the text of one row: the entry at list position HL, or a blank
; row when the position is past the end of the list. Positions outside the
; window are ignored, so callers need not check.
;
; Always in normal colours. The selection is a separate XOR pass, which is what
; makes moving it free of glyph work.
; -----------------------------------------------------------------------------
DrawRow:
    call    RowY
    ret     c
    ld      (CurY), a

    push    hl

    ; Past the end of the list: blank the row so a shorter directory cannot
    ; leave the previous one's names on screen.
    ld      de, (EntryCount)
    call    CmpHLDE
    jr      nc, .blank

    ; Build the whole row as one string, then put it out as a single band: the
    ; marker, the name, and spaces to the end so that whatever was there before
    ; is erased.
    call    ClearRowBuf

    pop     hl
    push    hl
    ld      b, h
    ld      c, l
    call    SortedEntryAddr
    push    hl

    ; Classify by the original filename, even when a name= label is shown.
    ld      a, (hl)
    or      a
    ld      a, (CfgColorDir)
    ld      hl, MsgDirMark
    jr      nz, .haveColor
    pop     hl
    push    hl
    ld      de, ENTRY_NAME
    add     hl, de
    call    IsDskPath
    ld      a, (CfgColorDsk)
    jr      z, .fileColor
    ld      a, (CfgColorRom)
.fileColor:
    ld      hl, MsgFileMark
    IFDEF AUTO_LOADER
    pop     de
    push    de
    push    af
    push    hl
    ex      de, hl
    call    IsDisk2Entry
    pop     hl
    jr      nz, .notDisk2
    ld      hl, MsgDiskMark
.notDisk2:
    pop     af
    ENDIF
.haveColor:
    push    hl
    ld      hl, RowColor
    cp      (hl)
    jr      z, .tableReady
    ld      (hl), a
    ld      hl, GlyphTableDir
    call    BuildOneTable
.tableReady:
    pop     hl
    ld      de, GlyphTableDir
    ld      (BandTable), de
    ld      de, RowBuf
    call    CopyToRowBuf

    pop     hl
    call    EntryDispStr
    ld      c, a                    ; nonzero for an INI label with CR/LF terminators
    ld      de, RowBuf + 2
    ld      b, LIST_WIDTH - 2       ; the row is fixed width; a longer name is
    call    CopyToRowBufN           ; shown cut, not allowed to run off the end
    jr      .paint

.blank:
    call    ClearRowBuf
    ld      hl, GlyphTable
    ld      (BandTable), hl

.paint:
    ld      hl, RowBuf
    ld      d, LIST_X
    ld      a, (CurY)
    ld      e, a
    ld      b, LIST_WIDTH
    call    DrawBand

    di
    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a
    ei
    pop     hl
    ret

; -----------------------------------------------------------------------------
; DrawScrollHints paints the two arrows in their own column: up beside the first
; row when the list continues above the window, down beside the last when it
; continues below, a space where it does not.
;
; Two characters, so it is affordable after every scroll -- which is necessary,
; because the column is outside the shifted rectangle and therefore never moves
; by itself.
; -----------------------------------------------------------------------------
DrawScrollHints:
    ld      hl, GlyphTable
    ld      (BandTable), hl

    ; Above: anything scrolled off the top?
    ld      hl, (ScrollTop)
    ld      a, h
    or      l
    ld      hl, PatBlank
    jr      z, .haveUp
    ld      hl, PatArrowUp
.haveUp:
    ld      d, HINT_X
    ld      e, LIST_Y0
    call    DrawPattern

    ; Below: anything past the last visible row?
    ld      hl, (ScrollTop)
    ld      de, LIST_ROWS
    add     hl, de
    ld      de, (EntryCount)
    call    CmpHLDE
    ld      hl, PatBlank
    jr      nc, .haveDown
    ld      hl, PatArrowDown
.haveDown:
    ld      d, HINT_X
    ld      e, LIST_Y0 + (LIST_ROWS - 1) * LIST_PITCH
    call    DrawPattern
    ret

; -----------------------------------------------------------------------------
; ClearRowBuf fills the row with spaces and terminates it.
; CopyToRowBuf copies a string into it without the terminator, so the spaces
; after it survive as padding.
; -----------------------------------------------------------------------------

    IFDEF AUTO_LOADER
MsgDiskMark: db 2," ",0
    ENDIF
