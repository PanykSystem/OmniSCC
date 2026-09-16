; EJECT.COM [A:|B:] -- leave a cartridge drive empty.
    ORG $0100
Start:
    call    ParseArgument
    jp      c, Usage
    ld      hl, (PathPtr)
    ld      a, (hl)
    or      a
    jp      nz, Usage
    call    FindCartridge
    jp      c, NoCartridge
    ; Flush DOS caches against the OLD medium, before setting API parameters.
    ld      c, $0D
    call    BDOS
    call    WriteDrive
    ld      a, CMD_EJECT
    ld      bc, 0
    call    ApiCall
    jp      FinishStatus
UsageText:
    db 'EJECT [A:|B:]',13,10,'Default: A:',13,10,0
    INCLUDE 'common.inc'
    ASSERT $ < $4000
