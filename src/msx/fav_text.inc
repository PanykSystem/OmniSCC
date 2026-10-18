; SCREEN 1 favourites use the browser's font, frame and safe TMS9918 writes.
FOOTER_CHARS EQU 30
    INCLUDE "text_frame.inc"

PrintableChar:
    cp 32
    jr c, .replace
    cp 128
    ret c
.replace:
    ld a, '?'
    ret

DrawFavList:
    di
    ld ix, FavPointers
    ld c, 0
    ld e, 4
.slot:
    ld l, (ix+0)
    ld h, (ix+1)
    ld a, h
    or l
    jr z, .next
    push bc
    push de
    push ix
    ld a, c
    add a, '0'
    ld (FavBandText), a
    ld a, ':'
    ld (FavBandText+1), a
    ld a, ' '
    ld (FavBandText+2), a
    ld l, (ix+2)
    ld h, (ix+3)
    ld a, h
    or l
    jr nz, .label
    ld l, (ix+0)
    ld h, (ix+1)
.label:
    ld de, FavBandText+3
    ld b, 25
.copy:
    ld a, (hl)
    cp '['
    call z, SkipMapperTag
    or a
    jr z, .space
    cp 13
    jr z, .space
    cp 10
    jr z, .space
    cp ','
    jr z, .space
    inc hl
    call PrintableChar
    jr .store
.space:
    ld a, ' '
.store:
    ld (de), a
    inc de
    djnz .copy
    pop ix
    pop de
    pop bc
    ld a, (SelectedItem)
    cp c
    jr nz, .paint
    ld hl, FavBandText
    ld b, 28
.inverse:
    ld a, (hl)
    cp 128
    jr nc, .keep
    add a, 96
    ld (hl), a
.keep:
    inc hl
    djnz .inverse
.paint:
    push bc
    push de
    ld hl, FavBandText
    ld d, 2
    ld b, 28
    call DrawTextTiles
    pop de
    pop bc
    inc e
.next:
    inc ix
    inc ix
    inc ix
    inc ix
    inc c
    ld a, c
    cp 10
    jp nz, .slot
    ei
    ret

DrawFooter:
    ld hl, TextFooterInfo
    ld d, (32 - (TextFooterInfoEnd - TextFooterInfo - 1)) / 2
    ld e, 22
    ld b, TextFooterInfoEnd - TextFooterInfo - 1
    call DrawTextTiles
    ei
    ret

ClearCountdown:
    ld hl, BlankCount
    jr PaintCountdown
DrawCountdown:
    ld hl, CountStr
PaintCountdown:
    ld de, $1B01
    ld b, 3
    call DrawTimerString
    ei
    ret
BlankCount: db 0

TextFooterInfo: db "[F1] Help ",248," [RET] Open",0
TextFooterInfoEnd:
