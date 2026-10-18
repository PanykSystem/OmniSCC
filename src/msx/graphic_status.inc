; Graphical browser presentation; also included in the upper universal ROM.
DrawCountdown:
    ; Off, or nothing to boot: blank the field once and leave it alone.
    ld      a, (CountdownStop)
    or      a
    jr      nz, .blank
    ld      a, (AutoBoot)
    or      a
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

    ld      h, 0
    ld      l, c
    ld      de, CountStr
    call    AppendNumber
    xor     a
    ld      (de), a
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

; -----------------------------------------------------------------------------
; DrawFooter: the key hints, or the reason the list is not what the directory
; holds.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; Interrupts OFF around anything that writes the VDP. vdp.inc says so in its
; header -- "the callers keep them disabled around rendering" -- and this routine
; did not, which is where the random yellow dots came from.
;
; A VRAM write address is two bytes to port 0x99. The BIOS interrupt handler
; writes that port too, to select the status register it reads; an interrupt
; landing between our two bytes leaves the VDP pairing them with the handler's,
; so the address is never set and the glyph bytes that follow go wherever the
; address counter happened to be. One interrupt, one line of a character
; scattered across VRAM -- in this routine's colour, which is why the dots were
; yellow rather than white.
;
; DrawBand and DrawPattern hold the line themselves, per pixel line, so their
; callers need nothing. Everything that goes through vdp.inc's renderers or its
; command-block writer needs this.
; -----------------------------------------------------------------------------
DrawFooter:
    IFDEF AUTO_LOADER
    xor     a
    ld      (DiskStatusVisible), a
    ENDIF
    di
    ld      a, COLOR_FOOTER
    ld      (TextColor), a
    ld      a, COLOR_BG
    ld      (TextBgColor), a
    IFDEF AUTO_LOADER
    ; Clear only the margins, so ordinary repaints never blank the cyan star.
    ld      hl, PatBlank
    ld      d, LIST_X
    ld      e, FOOTER_Y
    ld      b, 3
    call    DrawStringPadded
    ld      hl, PatBlank
    ld      d, FOOTER_X + 22 * 8
    ld      e, FOOTER_Y
    ld      b, 3
    call    DrawStringPadded
    ENDIF

    ld      a, (DirError)
    or      a
    ld      hl, MsgDirError
    jr      nz, .haveMsg

    ld      a, (EntryCapped)
    or      a
    ld      hl, MsgKeys
    jr      z, .haveMsg
    ld      hl, MsgCapped

.haveMsg:
    ld      d, FOOTER_X
    ld      e, FOOTER_Y
    IFDEF AUTO_LOADER
    ld a, (DirError)
    ld b, a
    ld a, (EntryCapped)
    or b
    jr nz, .ordinaryMessage
    call DrawFooterKeys
    jr .doneKeys
.ordinaryMessage:
    ENDIF
    call    DrawString8x8
    IFDEF AUTO_LOADER
.doneKeys:
    ENDIF
    ei
    ret

; -----------------------------------------------------------------------------
; Drive B temporarily replaces the key hints after INS/DEL. The next input
; restores the centered Help/Open hint; directory redraws do the same.
; -----------------------------------------------------------------------------
DrawDisk2:
    IFDEF AUTO_LOADER
    ld      a, 1
    ld      (DiskStatusVisible), a
    ENDIF
    di
    ld      a, COLOR_FOOTER
    ld      (TextColor), a
    ld      a, COLOR_BG
    ld      (TextBgColor), a

    ld      hl, RowBuf
    ld      (hl), 'B'
    inc     hl
    ld      (hl), ':'
    inc     hl
    ex      de, hl                  ; DE -> where the name goes

    ld      a, (Disk2Path)
    or      a
    ld      hl, MsgNoDisk2
    jr      z, .haveText
    ld      hl, Disk2Path
.haveText:
    IFDEF AUTO_LOADER
    ld      b, 26                  ; 28 cells including B:, within 32-byte RowBuf
    ELSE
    ld      b, FOOTER_CHARS - 2     ; the line is fixed width, and RowBuf is not
    ENDIF
    call    StrCopyN                ; long enough for a whole path either

    ld      hl, RowBuf
    IFDEF AUTO_LOADER
    ld      d, LIST_X
    ld      e, FOOTER_Y
    ld      b, 28
    ELSE
    ld      d, FOOTER_X
    ld      e, FOOTER_Y
    ld      b, FOOTER_CHARS
    ENDIF
    call    DrawStringPadded

    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a
    ei
    ret

; -----------------------------------------------------------------------------
; DrawReading / ClearReading bracket the one operation slow enough to notice.
; -----------------------------------------------------------------------------
DrawReading:
    di
    ld      a, 15
    ld      (TextColor), a
    ld      a, 4
    ld      (TextBgColor), a
    ld      hl, MsgReading
    ld      d, LIST_X
    ld      e, LIST_Y0
    ld      b, LIST_WIDTH
    call    DrawStringPadded
    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a
    ei
    ret

ClearReading:
    ret                             ; DrawList overwrites the row

; =============================================================================
; INI
; =============================================================================

; -----------------------------------------------------------------------------
; InitHiliteCmd fills the fixed half of the highlight's VDP block: the rectangle
; spans a row of the list, the mask swaps 4 and 15, and the command is a logical
; fill with XOR. Only DY changes per call, which HiliteRow patches.
; -----------------------------------------------------------------------------
InitHiliteCmd:
    ld      hl, HiliteTemplate
    ld      de, HiliteCmd
    ld      bc, 11
    ldir
    ld      hl, ShiftTemplate
    ld      de, ShiftCmd
    ld      bc, 15
    ldir
    ret
