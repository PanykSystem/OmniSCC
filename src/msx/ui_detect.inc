    IFNDEF FAV_TEXT
; All dispatch and VRAM probing live in the permanent lower ROM.
UiText      EQU $D4B2           ; after FwHelpText, before EntryTable
ProbeLow    EQU $D4B3
ProbeHigh   EQU $D4B4
ProbeOk     EQU $D4B5
    ENDIF
SETWRT      EQU $0053

DetectUi:
    di
    ld      a, 1
    ld      (UiText), a         ; conservative default, including MSX1 + V9938
    ld      a, ($002D)          ; BIOS MSXVER: 0/1/2/3
    or      a
    ret     z
    cp      4
    ret     nc
    ld      c, 0
    call    ProbeRead
    ld      (ProbeLow), a
    ld      c, 1
    call    ProbeRead
    ld      (ProbeHigh), a
    ld      c, 0
    ld      b, $55
    call    ProbeWrite
    ld      c, 1
    ld      b, $AA
    call    ProbeWrite
    xor     a
    ld      (ProbeOk), a
    ld      c, 0
    call    ProbeRead
    cp      $55
    jr      nz, .restore
    ld      c, 1
    call    ProbeRead
    cp      $AA
    jr      nz, .restore
    ld      a, 1
    ld      (ProbeOk), a
.restore:
    ; Reverse order also restores correctly when the addresses alias.
    ld      a, (ProbeHigh)
    ld      b, a
    ld      c, 1
    call    ProbeWrite
    ld      a, (ProbeLow)
    ld      b, a
    ld      c, 0
    call    ProbeWrite
    ld      a, ($FFED)          ; BIOS R#14 shadow, unchanged by our probe
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a
    ld      a, (ProbeOk)
    xor     1
    ld      (UiText), a
    ret

ProbeBank:
    ld      a, c
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a
    xor     a
    out     (VDP_CTRL), a
    ret
ProbeRead:
    call    ProbeBank
    xor     a
    out     (VDP_CTRL), a
    nop
    nop
    nop
    in      a, (VDP_DATA)
    ret
ProbeWrite:
    call    ProbeBank
    ld      a, $40
    out     (VDP_CTRL), a
    nop
    nop
    nop
    ld      a, b
    out     (VDP_DATA), a
    ret

