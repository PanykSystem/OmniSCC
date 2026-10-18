; Exact key match. Carry set = our key (valid or invalid), clear = another key.
; Invalid values leave the previous/default setting intact.
ParseSccVolumeLine:
    push hl
    ld de, SccVolumeKey
.key:
    ld a, (de)
    or a
    jr z, .matched
    cp (hl)
    jr nz, .other
    inc hl
    inc de
    jr .key
.other:
    pop hl
    or a
    ret
.matched:
    pop de
.space:
    ld a, (hl)
    cp ' '
    jr z, .nextSpace
    cp 9
    jr nz, .number
.nextSpace:
    inc hl
    jr .space
.number:
    ld bc, 0                       ; B = value, C = digit count
.digit:
    ld a, (hl)
    sub '0'
    cp 10
    jr nc, .endNumber
    ld d, a
    ld a, b
    cp 11                          ; next decimal digit would exceed 100
    jr nc, .invalid
    add a, a
    ld b, a
    add a, a
    add a, a
    add a, b
    add a, d
    cp 101
    jr nc, .invalid
    ld b, a
    inc c
    jr z, .invalid                 ; do not wrap the digit count
    inc hl
    jr .digit
.endNumber:
    ld a, c
    or a
    jr z, .invalid
.trailing:
    ld a, (hl)
    or a
    jr z, .store
    cp 13
    jr z, .store
    cp 10
    jr z, .store
    cp ';'
    jr z, .store
    cp '#'
    jr z, .store
    cp ' '
    jr z, .next
    cp 9
    jr nz, .invalid
.next:
    inc hl
    jr .trailing
.store:
    ld a, b
    ld (CfgSccVolume), a
.invalid:
    scf
    ret

ApplySccVolume:
    ld a, (CfgSccVolume)
    ld (CART_BUF), a
    ld a, $07                      ; CMD_SET_SCC_VOLUME
    jp ExecCartCmd
SccVolumeKey: db "scc_volume=", 0
