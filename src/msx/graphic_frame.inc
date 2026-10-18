; Graphical browser presentation; also included in the upper universal ROM.
DrawHelp:
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
    IFDEF AUTO_LOADER
    ld      a, (HelpActive)
    cp      2
    jr      nz, .line
    ld      hl, CreditsLines
    ld      e, 58                   ; centre the 108-pixel credits block
    ENDIF
.line:
    ld      a, (hl)
    or      a
    jr      z, .done
    push    hl
    push    de
    IFDEF AUTO_LOADER
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
    ENDIF
.drawLine:
    IFDEF AUTO_LOADER
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
    ENDIF
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
    IFDEF AUTO_LOADER
    ld      a, (HelpActive)
    cp      2
    call    nz, DrawFwVersion
    ELSE
    call    DrawFwVersion
    ENDIF
    ei
    ret

    INCLUDE "menu_art.inc"
    IFNDEF AUTO_LOADER
    INCLUDE "fw_help.inc"
    ENDIF

; -----------------------------------------------------------------------------
; Interrupts are off around every one of these, and that is not optional: see
; the banner above DrawFooter.
; -----------------------------------------------------------------------------
DrawFrame:
    di
    ld      hl, CmdShadow
    call    DrawBoxVDP
    ld      hl, CmdBorder
    call    DrawBoxVDP
    ld      hl, CmdFill
    call    DrawBoxVDP
    call    WaitVDP
    call    DrawRoundedCorners

    call    DrawTitle

    jp      DrawPath                ; which disables them around its own drawing

DrawRoundedCorners:
    ld      hl, GlyphTable
    ld      (BandTable), hl
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

; -----------------------------------------------------------------------------
; DrawPath shows the directory being listed, in the header. A path longer than
; the field is shown by its tail -- the end is the part that identifies it.
; -----------------------------------------------------------------------------
DrawPath:
    ld      a, 10
    ld      (TextColor), a
    ld      a, 4
    ld      (TextBgColor), a

    ld      hl, CurPath
    call    StrLen
    ld      a, b
    cp      PATH_CHARS + 1
    jr      c, .short
    ld      c, b
    ld      b, 0
    add     hl, bc                  ; HL -> the terminator
    ld      bc, PATH_CHARS
    or      a
    sbc     hl, bc                  ; HL -> the last PATH_CHARS characters

.short:
    di
    ld      d, PATH_X
    ld      e, PATH_Y
    ld      b, PATH_CHARS
    call    DrawStringPadded
    ei
    ret
