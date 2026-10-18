; Private scratch avoids the favourites glyph tables at D000-DFFF.
UiText EQU $C99C
ProbeLow EQU $C99D
ProbeHigh EQU $C99E
ProbeOk EQU $C99F
    INCLUDE "ui_detect.inc"
InitAutoScreen:
    ld a, (UiText)
    or a
    jp nz, FavText.InitTextScreen
    xor a
    ld (DPPAGE), a
    ld (ACPAGE), a
    ld a, 5
    call CHGMOD
    jp HideStartupScreen
