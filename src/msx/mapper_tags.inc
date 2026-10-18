; HL points at '['. Skip only exact recognized tags, preserving BC and DE.
; Adjacent mapper tags are skipped together; arbitrary brackets remain visible.
SkipMapperTag:
    push bc
    push de
    push hl
    ld de, MapperTagNames
.nextName:
    pop hl
    push hl
    inc hl
.compare:
    ld a, (de)
    or a
    jr z, .nameEnd
    call UpperCase
    ld b, a
    ld a, (hl)
    call UpperCase
    cp b
    jr nz, .skipName
    inc de
    inc hl
    jr .compare
.nameEnd:
    ld a, (hl)
    cp ']'
    jr nz, .skipName
    inc hl
    pop af                       ; discard original HL
    pop de
    pop bc
    ld a, (hl)
    cp '['
    jr z, SkipMapperTag
    ret
.skipName:
    ld a, (de)
    inc de
    or a
    jr nz, .skipName
    ld a, (de)
    or a
    jr nz, .nextName
    pop hl
    pop de
    pop bc
    ld a, (hl)
    ret
    INCLUDE "../generated/mapper_tags.inc"
