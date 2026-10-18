; Shared exact INI key. Invalid values leave the previous/default value intact.
ParseMultidiskLine:
    ld de, MultidiskKey
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
    jr nz, .digit
.space:
    inc hl
    jr .value
.digit:
    sub '0'
    cp 2
    ret nc
    ld b, a
    inc hl
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
    ret nz
.next:
    inc hl
    jr .trailing
.store:
    ld a, b
    ld (CfgMultidisk), a
    ret

ApplyDiskOptions:
    ld a, (CfgMultidisk)
    ld (CART_BUF), a
    ld a, $06                    ; CMD_SET_DISK_OPTIONS, bit 0 = multidisk
    jp ExecCartCmd               ; no reply payload/overlay
MultidiskKey: db "multidisk=", 0
