; Keep BIOS R1 shadow and the VDP in agreement, so interrupts cannot reveal
; a partially drawn menu. Called only with the exchange overlay closed.
HideStartupScreen:
    di
    ld a, ($F3E0)
    and $BF
    jr SetStartupDisplay
ShowStartupScreen:
    di
    ld a, (UiText)
    or a
    jr nz, .ready
    IFDEF AUTO_LOADER
    call GraphicUI.WaitVDP
    ELSE
    call WaitVDP
    ENDIF
    ; The menu was painted on page 0 while page 1 displayed the splash.
    xor a
    ld (DPPAGE), a
    ld a, ($F3E1)
    and $1F
    ld ($F3E1), a
    out (VDP_CTRL), a
    ld a, $82
    out (VDP_CTRL), a
.ready:
    ld a, ($F3E0)
    or $40
SetStartupDisplay:
    ld ($F3E0), a
    out (VDP_CTRL), a
    ld a, $81
    out (VDP_CTRL), a
    ret

; Zero means an interactive menu with no countdown. Positive delays retain
; their normal expiry behaviour after the counter has been enabled.
DisableZeroDelay:
    ld hl, (CfgDelay)
    ld a, h
    or l
    ret nz
    inc a
    ld (CountdownStop), a
    ret

; Bespoke four-point sparkle, shared by SCREEN 1 and SCREEN 5.
FooterStarPattern: db $10,$10,$38,$FE,$38,$10,$10,$00

; Show only the shared wordmark on page 1 before filesystem/INI initialization.
; Page 0 remains the renderer's target throughout, including BIOS interrupts.
DrawStartupSplash:
    ld a, (UiText)
    or a
    ret nz
    di
    call HasSplashPage
    or a
    ret z
    ld hl, StartupSplashClear
    call StartupDrawBox
    call StartupWaitVDP
    ld de, $8000 + ((212-StartupLogoHeight)/2)*128 + (256-StartupLogoWidth)/4
    call StartupDrawLogoAt
    di
    ld a, 1
    ld (DPPAGE), a
    ld a, ($F3E1)
    and $1F
    or $20
    ld ($F3E1), a
    out (VDP_CTRL), a
    ld a, $82
    out (VDP_CTRL), a
    ; BIOS VRAM routines keep their normal page-0 write address.
    call ResetStartupWritePage
    ld a, ($F3E0)
    or $40
    jp SetStartupDisplay

; Keep the splash visible while the complete menu is drawn on page 0.
; Text mode retains its original second initialization pass.
PrepareStartupMenu:
    ld a, (UiText)
    or a
    jp nz, InitAutoScreen
    di
    xor a
    ld (ACPAGE), a
    ld hl, StartupMenuClear
    call StartupDrawBox
    call StartupWaitVDP
ResetStartupWritePage:
    xor a
    out (VDP_CTRL), a
    ld a, $8E
    out (VDP_CTRL), a
    ret

StartupSplashClear:
    dw 0, 256, 256, 212
    db $44, 0, $C0
StartupMenuClear:
    dw 0, 0, 256, 212
    db $44, 0, $C0

    IFDEF AUTO_LOADER
StartupDrawBox EQU GraphicUI.DrawBoxVDP
StartupWaitVDP EQU GraphicUI.WaitVDP
StartupDrawLogoAt EQU GraphicUI.DrawStartupLogoAt
StartupLogoWidth EQU GraphicUI.STARTUP_LOGO_WIDTH
StartupLogoHeight EQU GraphicUI.STARTUP_LOGO_HEIGHT
    ELSE
StartupDrawBox EQU DrawBoxVDP
StartupWaitVDP EQU WaitVDP
StartupDrawLogoAt EQU DrawStartupLogoAt
StartupLogoWidth EQU STARTUP_LOGO_WIDTH
StartupLogoHeight EQU STARTUP_LOGO_HEIGHT
    ENDIF

; Probe page 1 without changing existing VRAM bytes. Graphics on a 32 KiB
; VDP remains supported, but has no independent splash page to display.
HasSplashPage:
    ld c, 0
    call ProbeRead
    ld (ProbeLow), a
    ld c, 2
    call ProbeRead
    ld (ProbeHigh), a
    ld c, 0
    ld b, $55
    call ProbeWrite
    ld c, 2
    ld b, $AA
    call ProbeWrite
    xor a
    ld (ProbeOk), a
    ld c, 0
    call ProbeRead
    cp $55
    jr nz, .restore
    ld c, 2
    call ProbeRead
    cp $AA
    jr nz, .restore
    ld a, 1
    ld (ProbeOk), a
.restore:
    ld a, (ProbeHigh)
    ld b, a
    ld c, 2
    call ProbeWrite
    ld a, (ProbeLow)
    ld b, a
    ld c, 0
    call ProbeWrite
    call ResetStartupWritePage
    ld a, (ProbeOk)
    ret
