; Shared non-blocking SCC volume popup. Settings are written on boot/exit only.
VolumeUpKey:
    call VolumeUp
    jp PollLoop
VolumeDownKey:
    call VolumeDown
    jp PollLoop
VolumeUp:
    ld a, (CfgSccVolume)
    add a, 10
    cp 101
    jr c, SetMenuVolume
    ld a, 100
    jr SetMenuVolume
VolumeDown:
    ld a, (CfgSccVolume)
    sub 10
    jr nc, SetMenuVolume
    xor a
SetMenuVolume:
    ld b, a
    ld a, (CfgSccVolume)
    cp b
    jr z, .unchanged
    ld a, b
    ld (CfgSccVolume), a
    ld a, 1
    ld (VolumeDirty), a
.unchanged:
    ld a, 1
    ld (CountdownStop), a
    call ApplySccVolume
    ld a, 100
    ld (VolumeFrames), a
    jp PaintVolume

TickVolume:
    ld a, (VolumeFrames)
    or a
    ret z
    dec a
    ld (VolumeFrames), a
    ret nz
    jp ClearVolume
DismissVolume:
    ld a, (VolumeFrames)
    or a
    ret z
    xor a
    ld (VolumeFrames), a
    jp ClearVolume

BuildVolumeText:
    ld hl, VolumeText
    ld (hl), ' '
    ld de, VolumeText+1
    ld bc, 15
    ldir
    xor a
    ld (VolumeText+16), a
    ld hl, VolumeCaption
    ld de, VolumeText
.copy:
    ld a, (hl)
    or a
    jr z, .number
    ld (de), a
    inc hl
    inc de
    jr .copy
.number:
    ld a, (CfgSccVolume)
    cp 100
    jr nz, .small
    ld a, '1'
    ld (de), a
    inc de
    ld a, '0'
    ld (de), a
    inc de
    jr .units
.small:
    ld b, '0'
.tens:
    cp 10
    jr c, .haveTens
    sub 10
    inc b
    jr .tens
.haveTens:
    ld c, a
    ld a, b
    cp '0'
    jr z, .oneDigit
    ld (de), a
    inc de
.oneDigit:
    ld a, c
    add a, '0'
.units:
    ld (de), a
    inc de
    ld a, '%'
    ld (de), a
    ld hl, VolumeText
    ret
VolumeCaption: db "SCC Volume: ", 0

PaintVolume:
    call BuildVolumeText
    ld a, (UiText)
    or a
    jr z, .graphic
    di
    ld hl, VolumeBorder
    ld de, $060A
    ld b, 20
    call VolumeDrawText
    ld hl, VolumeSides
    ld de, $060B
    ld b, 20
    call VolumeDrawText
    ld hl, VolumeBorder
    ld de, $060C
    ld b, 20
    call VolumeDrawText
    ld hl, VolumeText
    ld de, $080B
    ld b, 16
    call VolumeDrawText
    ei
    ret
.graphic:
    di
    ld hl, VolumeOuter
    call VolumeDrawBox
    call VolumeWait
    ld hl, VolumeInner
    call VolumeDrawBox
    call VolumeWait
    ld a, 15
    ld (TextColor), a
    ld a, 4
    ld (TextBgColor), a
    ld hl, VolumeText
    ld de, $4060                  ; x=64, y=96
    ld b, 16
    call VolumeDrawGraphic
    ei
    ret
VolumeBorder: db "+------------------+",0
VolumeSides: db "|                  |",0
VolumeOuter: dw 40,80,176,40
    db $FF,0,$C0
VolumeInner: dw 42,82,172,36
    db $44,0,$C0

ClearVolume:
    call DrawFrame
 IF VOLUME_BROWSER
    call DrawList
 ELSE
    call DrawFavList
 ENDIF
    jp DrawFooter

PersistVolume:
    ld a, (VolumeDirty)
    or a
    ret z
 IF VOLUME_BROWSER
    call SaveIni
 ELSE
    call RunSaveVolumeStub
    ret c                         ; a full INI is left untouched
 ENDIF
    xor a
    ld (VolumeDirty), a
    ret

 IF VOLUME_BROWSER
VolumeDrawText EQU TextUI.DrawStringPadded
VolumeDrawGraphic EQU GraphicUI.DrawStringPadded
VolumeDrawBox EQU GraphicUI.DrawBoxVDP
VolumeWait EQU GraphicUI.WaitVDP
 ELSE
VolumeDrawText EQU FavText.DrawStringPadded
VolumeDrawGraphic EQU DrawStringPadded
VolumeDrawBox EQU DrawBoxVDP
VolumeWait EQU WaitVDP
 ENDIF

JustHelpVersion:
    ld hl, FwHelpText
    ret

; Resolve dynamic modal rows without changing the caller's drawing position.
ResolveHelpLine:
    push de
    ld de, CreditsVersionLine
    or a
    sbc hl, de
    add hl, de
    pop de
    jr z, JustHelpVersion
    push de
    ld de, HelpVolumeLine
    or a
    sbc hl, de
    add hl, de
    pop de
    ret nz
    push de
    push bc
    call BuildVolumeText
    ; Extend the popup's 12-byte prefix to the 18-byte Help binding. The
    ; backwards copy preserves digits, padding and NUL in the shared scratch.
    ld hl, VolumeText+16
    ld de, VolumeText+22
    ld bc, 5
    lddr
    ld hl, HelpVolumeCaption
    ld de, VolumeText
    ld bc, 18
    ldir
    ld hl, VolumeText
    pop bc
    pop de
    ret
HelpVolumeCaption: db "[+/-] SCC volume: "
