DrawCountdown:
    ld      a, (CountdownStop)
    or      a
    jr      nz, .blank
    ld      a, (AutoBoot)
    or      a
    jr      z, .blank
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
    ret     z
    ld      a, c
    ld      (LastSecs), a
    ld      h, 0
    ld      l, c
    ld      de, CountStr
    call    AppendNumber
    xor     a
    ld      (de), a
    ld      hl, CountStr
    jr      .draw
.blank:
    ld      a, (LastSecs)
    cp      $FF
    ret     z
    ld      a, $FF
    ld      (LastSecs), a
    ld      hl, TextBlank
.draw:
    ld      d, COUNT_X
    ld      e, COUNT_Y
    ld      b, 5
    call    DrawTimerString
    ei
    ret

DrawFooter:
    xor     a
    ld      (DiskStatusVisible), a
    ld      a, (DirError)
    or      a
    ld      hl, MsgDirError
    jr      nz, .message
    ld      a, (EntryCapped)
    or      a
    ld      hl, MsgCapped
    jr      nz, .message
    ; Remove a previous B: path outside the centered hint without blanking it.
    ld      hl, TextBlank
    ld      d, 0
    ld      e, FOOTER_Y
    ld      b, 5
    call    DrawTextTiles
    ld      hl, TextBlank
    ld      d, 27
    ld      e, FOOTER_Y
    ld      b, 5
    call    DrawTextTiles
    ld      hl, TextMoreKeys
    ld      d, (32 - (TextMoreKeysEnd - TextMoreKeys - 1)) / 2
    ld      e, FOOTER_Y
    ld      b, TextMoreKeysEnd - TextMoreKeys - 1
    call    DrawTextTiles
    ei
    ret
.message:
    ld      d, 0
    ld      e, FOOTER_Y
    ld      b, 32
    call    DrawStringPadded
    ei
    ret

DrawDisk2:
    ld      a, 1
    ld      (DiskStatusVisible), a
    ld      hl, RowBuf
    ld      (hl), 'B'
    inc     hl
    ld      (hl), ':'
    inc     hl
    ex      de, hl
    ld      a, (Disk2Path)
    or      a
    ld      hl, MsgNoDisk2
    jr      z, .text
    ld      hl, Disk2Path
.text:
    ld      b, FOOTER_CHARS-2
    call    StrCopyN
    ld      hl, RowBuf
    ld      d, 1
    ld      e, FOOTER_Y
    ld      b, 30
    call    DrawStringPadded
    ei
    ret

DrawReading:
    ld      hl, MsgReading
    ld      d, LIST_X
    ld      e, LIST_Y0
    ld      b, LIST_WIDTH
    call    DrawStringPadded
    ei
    ret
ClearReading:
InitHiliteCmd:
    ret

TextMoreKeys: db "[F1] Help ",248," [RET] Open",0
TextMoreKeysEnd:
