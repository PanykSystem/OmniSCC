    INCLUDE "ui_detect.inc"

InitAutoScreen:
    ld      a, (UiText)
    or      a
    jp      nz, TextUI.InitTextScreen
    xor     a
    ld      (DPPAGE), a
    ld      (ACPAGE), a
    ld      a, 5
    call    CHGMOD
    jp      HideStartupScreen
InitTextScreen:
    jp      TextUI.InitTextScreen
ResetVramPage:
    ld      a, (UiText)
    or      a
    ret     nz
    xor     a
    out     (VDP_CTRL), a
    ld      a, $8E
    out     (VDP_CTRL), a
    ret

    MACRO UI_DISPATCH name, text_target, graphic_target
name:
    push    af
    ld      a, (UiText)
    or      a
    jr      z, .graphic
    pop     af
    jp      text_target
.graphic:
    pop     af
    jp      graphic_target
    ENDM
    UI_DISPATCH DrawFrame, TextUI.DrawFrame, GraphicUI.DrawFrame
    UI_DISPATCH DrawPath, TextUI.DrawPath, GraphicUI.DrawPath
    UI_DISPATCH DrawHelp, TextUI.DrawHelp, GraphicUI.DrawHelp
    UI_DISPATCH DrawRow, TextUI.DrawRow, GraphicUI.DrawRow
    UI_DISPATCH RowY, TextUI.RowY, GraphicUI.RowY
    UI_DISPATCH HiliteRow, TextUI.HiliteRow, GraphicUI.HiliteRow
    UI_DISPATCH DrawScrollHints, TextUI.DrawScrollHints, GraphicUI.DrawScrollHints
    UI_DISPATCH DrawCountdown, TextUI.DrawCountdown, GraphicUI.DrawCountdown
    UI_DISPATCH DrawFooter, TextUI.DrawFooter, GraphicUI.DrawFooter
    UI_DISPATCH DrawDisk2, TextUI.DrawDisk2, GraphicUI.DrawDisk2
    UI_DISPATCH DrawReading, TextUI.DrawReading, GraphicUI.DrawReading
    UI_DISPATCH ClearReading, TextUI.ClearReading, GraphicUI.ClearReading
    UI_DISPATCH InitHiliteCmd, TextUI.InitHiliteCmd, GraphicUI.InitHiliteCmd
    UI_DISPATCH BuildGlyphTable, TextUI.BuildGlyphTable, GraphicUI.BuildGlyphTable
    UI_DISPATCH BuildOneTable, TextUI.BuildOneTable, GraphicUI.BuildOneTable
    UI_DISPATCH DrawStringPadded, TextUI.DrawStringPadded, GraphicUI.DrawStringPadded
    UI_DISPATCH DrawString8x8, TextUI.DrawString8x8, GraphicUI.DrawString8x8
ShiftList:
    jp      GraphicUI.ShiftList ; only reached by the graphical MoveTo path
