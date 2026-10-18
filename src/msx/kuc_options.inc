; Exact boolean key; invalid input leaves the previous/default option intact.
ParseKucOptionsLine:
    ld de, KucOptionsKey
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
    ld (CfgKucOptions), a
    ret

ApplyKucOptions:
    ld a, (CfgKucOptions)
    ld (CART_BUF), a
    ld a, $09                     ; CMD_SET_KUC_OPTIONS, bit 0 = unknown flash
    jp ExecCartCmd
KucOptionsKey: db "kuc_allow_unknown_flash=", 0
