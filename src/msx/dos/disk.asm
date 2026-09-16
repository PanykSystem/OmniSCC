; DISK.COM -- show both cartridge drives, using CMD_BASIC_INFO.
    ORG $0100
Start:
    call    ParseArgument
    jp      c, Usage
    ld      a, (ArgLength)
    or      a
    jp      nz, Usage
    call    FindCartridge
    jp      c, NoCartridge
    ld      a, CMD_INFO
    ld      bc, INFO_SIZE
    call    ApiCall
    or      a
    jp      nz, FinishStatus
    ld      hl, Reply
    ld      a, 'A'
    call    PrintDrive
    ld      a, 'B'
    call    PrintDrive
    jp      Exit

; HL -> variable-length record, advanced to the next record on return.
PrintDrive:
    call    PrintChar
    ld      a, ':'
    call    PrintChar
    ld      a, ' '
    call    PrintChar
    ld      b, (hl)              ; state
    inc     hl
    ld      e, (hl)
    inc     hl
    ld      d, (hl)              ; sectors
    inc     hl
    bit     0, b
    jr      z, .empty
    call    PrintString         ; leaves HL just after path terminator
    push    hl
    push    bc
    ex      de, hl
    ld      a, ' '
    call    PrintChar
    call    PrintChar
    srl     h
    rr      l                   ; 512-byte sectors -> KiB
    push    af                  ; keep half-KiB remainder
    call    PrintNumber
    pop     af
    jr      nc, .whole
    ld      a, '.'
    call    PrintChar
    ld      a, '5'
    call    PrintChar
.whole:
    ld      a, 'K'
    call    PrintChar
    pop     bc
    bit     1, b
    jr      z, .unprotected
    ld      hl, ProtectedText
    call    PrintString
.unprotected:
    pop     hl
    jp      NewLine
.empty:
    ld      a, (hl)
    inc     hl
    or      a
    jr      nz, .empty
    push    hl
    ld      hl, EmptyText
    call    PrintString
    pop     hl
    jp      NewLine

UsageText:     db 'DISK',13,10,0
EmptyText:     db '-- empty --',0
ProtectedText: db '  (protected)',0
    INCLUDE 'common.inc'
    ASSERT $ < $4000
