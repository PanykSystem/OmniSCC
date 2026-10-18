; Shared 24x12 countdown badge and 8x8 digit renderer.
; Rounded filled badge. One edge byte at each end of its 12-byte rows;
; the ten middle bytes are white. Entirely under DI, like the digits.
DrawCountdownBadge:
    di
    ld      hl, COUNT_Y * 128 + COUNT_X / 2
    ld      de, CountdownEdges
    ld      c, 12
.row:
    call    SetVramWrite
    ld      a, (de)
    out     (VDP_DATA), a
    push    af
    inc     de
    ld      a, $FF
    ld      b, 10
.fill:
    out     (VDP_DATA), a
    djnz    .fill
    pop     af
    rrca
    rrca
    rrca
    rrca
    out     (VDP_DATA), a
    ld      a, l
    add     a, 128
    ld      l, a
    jr      nc, .noCarry
    inc     h
.noCarry:
    dec     c
    jr      nz, .row
    ret
CountdownEdges:
    db $44,$4F
    DUP 8
    db $FF
    EDUP
    db $4F,$44
CmdCountdownClear:
    dw COUNT_X, COUNT_Y, 24, 12
    db $44, 0, $C0

; Render the original 8x8 font without scaling.
; A = ASCII digit; HL = its VRAM address. Red ink on the white badge.
DrawCountdownDigit:
    di
    push    hl
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    add     hl, hl
    ld      de, Font8x8
    add     hl, de
    ex      de, hl
    pop     hl
    ld      c, 8
.row:
    call    SetVramWrite
    push    hl
    push    bc
    ld      a, (de)
    ld      c, a
    ld      b, 4
.pixel:
    sla     c
    ld      h, $80
    jr      c, .first
    ld      h, $F0
.first:
    sla     c
    ld      a, 8
    jr      c, .second
    ld      a, 15
.second:
    or      h
    out     (VDP_DATA), a
    djnz    .pixel
    inc     de
    pop     bc
    pop     hl
    ld      a, l
    add     a, 128
    ld      l, a
    jr      nc, .noCarry
    inc     h
.noCarry:
    dec     c
    jr      nz, .row
    ret
