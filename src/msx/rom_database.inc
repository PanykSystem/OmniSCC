; Shared menu configuration. Empty romdb= disables hash lookup.
InitRomDatabase:
    xor a
    ld (CfgKucOptions), a
    ld hl, RomDatabaseDefault
    ld de, RomDatabasePath
.copy:
    ld a, (hl)
    ld (de), a
    inc hl
    inc de
    or a
    jr nz, .copy
    ret
ParseRomDatabaseLine:
    ld de, RomDatabaseKey
.key:
    ld a, (de)
    or a
    jr z, .value
    cp (hl)
    ret nz
    inc hl
    inc de
    jr .key
.value:
    ld a, (hl)
    cp ' '
    jr z, .space
    cp 9
    jr nz, .start
.space:
    inc hl
    jr .value
.start:
    push hl
    ld b, 0
.length:
    ld a, (hl)
    or a
    jr z, .valid
    cp 13
    jr z, .valid
    cp 10
    jr z, .valid
    inc b
    ld a, b
    cp 65
    jr nc, .invalid
    inc hl
    jr .length
.invalid:
    pop hl
    ret
.valid:
    pop hl
    ld de, RomDatabasePath
    ld a, b
    or a
    jr z, .end
.copy:
    ld a, (hl)
    ld (de), a
    inc hl
    inc de
    djnz .copy
.end:
    xor a
    ld (de), a
    ret
ApplyRomDatabase:
    ld hl, RomDatabasePath
    ld de, CART_BUF
.copy:
    ld a, (hl)
    ld (de), a
    inc hl
    inc de
    or a
    jr nz, .copy
    ld a, $08                     ; CMD_SET_ROM_DATABASE
    jp ExecCartCmd
RomDatabaseKey: db "romdb=", 0
RomDatabaseDefault: db "/romdb.bin", 0
