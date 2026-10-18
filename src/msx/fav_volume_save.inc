; Separate from live INI pointers and graphic tables (CC00..DBFF).
VolumeSaveBuf EQU $DC00
VolumeSaveSize EQU $C9A4

; Preserve all lines except old exact scc_volume= keys, then append one value.
; Input/parsed favourite pointers are never moved. Carry = would exceed reader.
PrepareVolumeSave:
    ; Respect the BIOS stack/disk buffers instead of assuming F380 is available.
    ld hl, (SavedSP)
    ld de, VolumeSaveBuf+2080+256
    or a
    sbc hl, de
    ret c
    ld hl, ($FC4A)                ; HIMEM also excludes other Disk ROM buffers
    or a
    sbc hl, de
    ret c
    ld hl, RAM_INI_BUF
    ld de, VolumeSaveBuf
.line:
    ld a, (hl)
    or a
    jr z, .append
    call .isVolumeKey
    jr z, .skip
.copy:
    ld a, (hl)
    or a
    jr z, .append
    ld (de), a
    inc hl
    inc de
    cp 13
    jr z, .line
    cp 10
    jr z, .line
    jr .copy
.skip:
    ld a, (hl)
    or a
    jr z, .append
    inc hl
    cp 10
    jr z, .line
    cp 13
    jr nz, .skip
    ld a, (hl)
    cp 10
    jr nz, .line
    inc hl
    jr .line
.append:
    ; Add a separator only when needed; repeated saves must not grow blank lines.
    push hl
    ld hl, VolumeSaveBuf
    or a
    sbc hl, de
    pop hl
    jr z, .keyStart
    dec de
    ld a, (de)
    inc de
    cp 10
    jr z, .keyStart
    cp 13
    jr z, .keyStart
    ld a, 13
    ld (de), a
    inc de
    ld a, 10
    ld (de), a
    inc de
.keyStart:
    ld hl, SccVolumeKey
.key:
    ld a, (hl)
    or a
    jr z, .value
    ld (de), a
    inc hl
    inc de
    jr .key
.value:
    push de
    call BuildVolumeText
    pop de
    ld hl, VolumeText+12
.number:
    ld a, (hl)
    cp '%'
    jr z, .end
    ld (de), a
    inc de
    inc hl
    jr .number
.end:
    ld a, 13
    ld (de), a
    inc de
    ld a, 10
    ld (de), a
    inc de
    xor a
    ld (de), a
    ex de, hl
    ld de, VolumeSaveBuf
    or a
    sbc hl, de
    ld (VolumeSaveSize), hl
    ld de, 2048                    ; reader accepts up to 2047 bytes
    or a
    sbc hl, de
    ccf
    ret
.isVolumeKey:
    push hl
    push de
.spaces:
    ld a, (hl)
    cp ' '
    jr z, .space
    cp 9
    jr nz, .matchStart
.space:
    inc hl
    jr .spaces
.matchStart:
    ld de, SccVolumeKey
.match:
    ld a, (de)
    or a
    jr z, .matched
    cp (hl)
    jr nz, .matched
    inc hl
    inc de
    jr .match
.matched:
    pop de
    pop hl
    ret
