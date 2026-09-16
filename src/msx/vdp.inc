; -----------------------------------------------------------------------------
; Screen 5 drawing, shared by every loader ROM.
;
; The V9938 command engine and an 8x8 text renderer that writes glyph pairs
; straight to VRAM. Included rather than copied because it is the fiddliest code
; in the menu and the least likely to need to differ between loaders.
;
; The includer must define:
;   TextX, TextY, TextColor, TextBgColor, GlyphBits   renderer scratch in RAM
;   Font8x8                                           the glyph table (font.inc)
;   CmdShadow, CmdBorder, CmdFill                     the box colour triples,
;                                                     which are policy and stay
;                                                     with the loader
; Interrupts: the callers keep them disabled around rendering, because the BIOS
; ISR also drives the VDP and would corrupt a multi-byte register sequence.
; -----------------------------------------------------------------------------

; =============================================================================
; VDP COMMAND ENGINE HELPERS
; =============================================================================
; These routines drive the V9938 VDP command engine directly. Interrupts are
; disabled around rendering because the MSX BIOS ISR also accesses the VDP;
; an interrupt in the middle of a multi-byte register sequence would corrupt
; the command and produce garbage on screen.

DrawBoxVDP:
    call    WaitVDP

    ld      a, 36
    out     (VDP_CTRL), a
    ld      a, $91
    out     (VDP_CTRL), a

    ld      c, VDP_INDIRECT
    ld      b, 11
    otir                            ; write 11 bytes of command data
    ret

WaitVDP:
    ld      a, 2
    out     (VDP_CTRL), a
    ld      a, $8F
    out     (VDP_CTRL), a           ; select status register 2

.wait:
    in      a, (VDP_CTRL)
    rrca
    jr      c, .wait                ; wait until CE (Command Execute) bit clears

    xor     a
    out     (VDP_CTRL), a
    ld      a, $8F
    out     (VDP_CTRL), a           ; restore status register 0
    ret

; -----------------------------------------------------------------------------
; Padded string renderer: fills a fixed width of B characters with spaces.
; -----------------------------------------------------------------------------

DrawStringPadded:
    ld      a, d
    ld      (TextX), a
    ld      a, e
    ld      (TextY), a

.charLoop:
    ld      a, (hl)
    or      a
    jr      z, .padSpace
    cp      13
    jr      z, .padSpace
    cp      10
    jr      z, .padSpace
    cp      ','
    jr      z, .padSpace

    inc     hl
    jr      .outChar

.padSpace:
    ld      a, ' '

.outChar:
    push    hl
    push    bc
    call    DrawChar8x8
    pop     bc
    pop     hl

    ld      a, (TextX)
    add     a, 8
    ld      (TextX), a

    djnz    .charLoop
    ret

; =============================================================================
; CUSTOM SCREEN 5 TEXT RENDERER
; =============================================================================
; The menu uses a custom 8x8 font instead of the BIOS font. Each glyph is
; rendered into 4 bytes of VRAM per row (8 pixels * 2 bits/pixel = 16 bits).
; Direct VRAM access is used for speed and to keep the screen format under
; full control.

DrawString8x8:
    ld      a, d
    ld      (TextX), a
    ld      a, e
    ld      (TextY), a
    ld      b, 24

.loop:
    ld      a, (hl)
    or      a
    ret     z
    cp      13
    ret     z
    cp      10
    ret     z

    push    hl
    push    bc
    call    DrawChar8x8
    pop     bc
    pop     hl

    inc     hl

    ld      a, (TextX)
    add     a, 8
    ld      (TextX), a

    djnz    .loop
    ret

DrawChar8x8:
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    add     hl, hl
    ld      de, Font8x8
    add     hl, de                  ; HL -> glyph data
    ex      de, hl

    ld      a, (TextY)
    ld      l, a
    ld      h, 0
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl
    add     hl, hl                  ; HL = Y * 128 (bytes per screen row)

    ld      a, (TextX)
    srl     a
    ld      c, a
    ld      b, 0
    add     hl, bc                  ; add X / 2 (2 pixels per byte)

    ld      b, 8

.rowLoop:
    push    bc
    push    de
    push    hl

    ld      a, (de)
    ld      (GlyphBits), a

    call    SetVramWrite

    call    GlyphPairToVRAM
    out     ($98), a
    call    GlyphPairToVRAM
    out     ($98), a
    call    GlyphPairToVRAM
    out     ($98), a
    call    GlyphPairToVRAM
    out     ($98), a

    pop     hl
    pop     de
    pop     bc

    inc     de

    ld      a, l
    add     a, 128
    ld      l, a
    jr      nc, .noCarry
    inc     h
.noCarry:
    djnz    .rowLoop
    ret

GlyphPairToVRAM:
    ld      a, (GlyphBits)
    rlca
    ld      (GlyphBits), a

    ld      a, (TextBgColor)
    jr      nc, .firstReady

    ld      a, (TextColor)

.firstReady:
    add     a, a
    add     a, a
    add     a, a
    add     a, a
    ld      c, a                    ; upper nibble = first pixel colour

    ld      a, (GlyphBits)
    rlca
    ld      (GlyphBits), a

    ld      a, (TextBgColor)
    jr      nc, .secondReady

    ld      a, (TextColor)

.secondReady:
    or      c                       ; combine both pixels
    ret

SetVramWrite:
    ld      a, h
    srl     a
    srl     a
    srl     a
    srl     a
    srl     a
    srl     a
    and     $07

    out     ($99), a
    ld      a, $8E
    out     ($99), a               ; register 14: upper VRAM address bits

    ld      a, l
    out     ($99), a

    ld      a, h
    and     $3F
    or      $40
    out     ($99), a               ; lower 14 bits + write flag
    ret
