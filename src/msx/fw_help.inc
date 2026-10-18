; Shared Help heading. FwHelpText reserves 16 RAM bytes in each menu.
; Called with interrupts disabled; the mailbox poll already lives in RAM.
CMD_GET_FW_VERSION EQU $05
FW_VERSION_MAX    EQU 12

ReadFwVersion:
    ld      hl, FwUnknown
    ld      de, FwHelpText
    ld      bc, 5
    ldir
    ld      a, CMD_GET_FW_VERSION
    call    ExecCartCmd
    or      a
    ret     nz                      ; old firmware or timeout: FW ?
    ld      hl, CART_BUF
    ld      de, FwHelpText+3
    ld      b, FW_VERSION_MAX
.copy:
    ld      a, (hl)
    or      a
    jr      z, .end
    cp      33                      ; version must be printable, no spaces
    jr      c, .invalid
    cp      127
    jr      nc, .invalid
    ld      (de), a
    inc     hl
    inc     de
    djnz    .copy
    ld      a, (hl)
    or      a
    jr      nz, .invalid            ; overlong response
.end:
    ld      a, b
    cp      FW_VERSION_MAX
    jr      z, .invalid             ; empty response
    xor     a
    ld      (de), a
    jr      .close
.invalid:
    ld      a, '?'
    ld      (FwHelpText+3), a
    xor     a
    ld      (FwHelpText+4), a
.close:
    xor     a                       ; CMD_CLOSE_BUFFER, before BIOS can run
    jp      ExecCartCmd

DrawFwVersion:
    ld      hl, FwHelpText
    ld      d, 224                  ; same right edge as the Help body
.width:
    ld      a, (hl)
    or      a
    jr      z, .draw
    ld      a, d
    sub     8
    ld      d, a
    inc     hl
    jr      .width
.draw:
    ld      e, 48                   ; same row as Help
    ld      hl, FwHelpText
    jp      DrawString8x8


FwUnknown: db "FW ?", 0
