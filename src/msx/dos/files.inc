; Shared client for outer-volume image operations. Parse the complete DOS tail
; before touching the cartridge. Commands 41h/42h start a bounded transfer;
; 43h advances it by one sector, returning 10h until complete.
FileStart:
    ld      a, ($80)
    cp      128
    jp      nc, Usage
    ld      b, a
    ld      hl, $81
    ld      de, FileArguments
    call    FileToken
    jp      c, Usage
    ld      a, (FileArguments)
    or      a
    jp      z, Usage
    ld      a, (FileArguments)
    cp      '/'
    jr      nz, .second
    ld      a, (FileArguments + 1)
    cp      '?'
    jp      z, Usage
.second:
    ld      de, FileArguments + 65
    call    FileToken
    jp      c, Usage
    ; Any remaining non-space byte is a third argument.
    call    FileSkip
    jp      nz, Usage
    ld      a, FILE_COMMAND
    cp      CMD_FILE_COPY
    jr      z, .copy
    cp      CMD_FILE_NEW
    jr      z, .new
    ld      a, (FileArguments + 65)
    or      a
    jp      nz, Usage
    jr      .send
.copy:
    ld      a, (FileArguments + 65)
    or      a
    jp      z, Usage
    jr      .send
.new:
    ld      de, 720
    ld      hl, FileArguments + 65
    ld      a, (hl)
    or      a
    jr      z, .size
    cp      '3'
    jr      z, .three
    cp      '7'
    jp      nz, Usage
    inc     hl
    ld      a, (hl)
    cp      '2'
    jr      .zero
.three:
    ld      de, 360
    inc     hl
    ld      a, (hl)
    cp      '6'
.zero:
    jp      nz, Usage
    inc     hl
    ld      a, (hl)
    cp      '0'
    jp      nz, Usage
    inc     hl
    ld      a, (hl)
    or      a
    jp      nz, Usage
.size:
    ld      (FileArguments + 130), de
.send:
    call    FindCartridge
    jp      c, NoCartridge
    ; DOS may overwrite all shared parameters while flushing. Do this first.
    ld      c, $0D
    call    BDOS
    di
    ld      hl, FileArguments
    ld      bc, CART_BUF
    ld      d, 132
.write:
    ld      e, (hl)
    inc     hl
    push    hl
    ld      h, b
    ld      l, c
    call    SlotWrite
    pop     hl
    inc     bc
    dec     d
    jr      nz, .write
    ld      a, FILE_COMMAND
.next:
    ld      bc, 0
    call    ApiCall
    cp      ST_FILE_MORE
    jp      nz, FinishStatus
    ld      a, CMD_FILE_STEP
    jr      .next

; B remaining bytes, HL input, DE destination (65 bytes). Carry on malformed
; token; Z on empty/missing token. Quoting encloses one entire token.
FileToken:
    call    FileSkip
    jr      z, .empty
    xor     a
    ld      (FileQuoted), a
    ld      c, 0
    ld      a, (hl)
    cp      '"'
    jr      nz, .loop
    ld      a, 1
    ld      (FileQuoted), a
    inc     hl
    dec     b
.loop:
    ld      a, b
    or      a
    jr      z, .end
    ld      a, (hl)
    inc     hl
    dec     b
    cp      '"'
    jr      z, .quote
    push    af
    ld      a, (FileQuoted)
    or      a
    jr      nz, .literal
    pop     af
    cp      ' '
    jr      z, .end
    cp      9
    jr      z, .end
    jr      .put
.literal:
    pop     af
.put:
    cp      32
    jr      c, .bad
    cp      ':'
    jr      z, .bad
    ld      (de), a
    inc     de
    inc     c
    ld      a, c
    cp      65
    jr      nc, .bad
    jr      .loop
.quote:
    ld      a, (FileQuoted)
    or      a
    jr      z, .bad
    xor     a
    ld      (FileQuoted), a
    ld      a, b
    or      a
    jr      z, .end
    ld      a, (hl)
    cp      ' '
    jr      z, .end
    cp      9
    jr      nz, .bad
.end:
    ld      a, (FileQuoted)
    or      a
    jr      nz, .bad
    ld      a, c
    or      a
    jr      z, .bad                 ; explicit "" is not a missing argument
    xor     a
    ld      (de), a
    ret
.empty:
    xor     a
    ld      (de), a
    ret
.bad:
    scf
    ret

FileSkip:
    ld      a, b
    or      a
    ret     z
    ld      a, (hl)
    cp      ' '
    jr      z, .space
    cp      9
    ret     nz
.space:
    inc     hl
    dec     b
    jr      FileSkip

FileQuoted: db 0
FileArguments: ds 132, 0
