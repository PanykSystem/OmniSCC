; OMOUNT.COM [A:|B:]path -- mount a DSK on the cartridge's own FAT volume.
    ORG $0100
Start:
    call    ParseArgument
    jp      c, Usage
    ld      hl, (PathPtr)
    ld      a, (hl)
    or      a
    jp      z, Usage
    call    FindCartridge
    jp      c, NoCartridge
    ; Flush DOS caches against the OLD medium, before setting API parameters.
    ld      c, $0D
    call    BDOS
    call    WritePath
    call    WriteDrive
    ld      a, CMD_MOUNT
    ld      bc, 0
    call    ApiCall
    jp      FinishStatus
UsageText:
    db 'OMOUNT [A:|B:]path.dsk',13,10
    db 'Quote paths containing spaces. Default: A:',13,10,0
    INCLUDE 'common.inc'
    ASSERT $ < $4000
