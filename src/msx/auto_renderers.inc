; 8x8 floppy disk with a clipped corner, top shutter and lower label.
; Shared by the graphical and SCREEN 1 browser.
DiskBPattern: db $FE,$C3,$DB,$FF,$81,$BD,$A5,$FF

; Z when this entry is the configured B: image (full path, case insensitive).
; Preserve row-renderer inputs; FullPath is scratch, never LastPath.
IsDisk2Entry:
    ld a, (hl)
    or a
    ret nz
    push hl
    push bc
    push de
    ld bc, ENTRY_NAME
    add hl, bc
    push hl
    ld hl, CurPath
    ld de, FullPath
    call StrCopy
    dec de
    dec de
    ld a, (de)
    inc de
    cp '/'
    jr z, .name
    ld a, '/'
    ld (de), a
    inc de
.name:
    pop hl
    call StrCopy
    ld hl, FullPath
    ld de, Disk2Path
    call StrEqNoCase
    pop de
    pop bc
    pop hl
    ret

; Upper ROM is inaccessible while the exchange overlay is open. No protocol
; helpers live here. ReadFwVersion is lower ROM and closes its overlay on return.
    MODULE GraphicUI
    INCLUDE "graphic_frame.inc"
    INCLUDE "graphic_list.inc"
    INCLUDE "graphic_status.inc"
    INCLUDE "vdp.inc"
Font8x8 EQU $-32*8
    INCLUDE "loader_font.inc"
Font8x8End:
    INCLUDE "loader_title.inc"
    INCLUDE "startup_logo.inc"
    ENDMODULE

    MODULE TextUI
LIST_Y0         EQU 4
LIST_PITCH      EQU 1
LIST_X          EQU 1
HINT_X          EQU 30
FOOTER_X        EQU 0
FOOTER_Y        EQU 22
COUNT_X         EQU 25
COUNT_Y         EQU 1
PATH_X          EQU 1
PATH_Y          EQU 3
PATH_CHARS      EQU 30
    INCLUDE "text_frame.inc"
    INCLUDE "text_list.inc"
    INCLUDE "text_status.inc"
    ENDMODULE
