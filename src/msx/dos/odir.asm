; ODIR.COM [path] -- list directories and DSKs on the cartridge FAT volume.
; Name avoids COMMAND.COM's built-in DIR. Ctrl-C or Esc stops the listing.
    ORG $0100
Start:
    call    ParseArgument
    jp      c, Usage
    call    FindCartridge
    jp      c, NoCartridge
    call    WritePath
    ld      a, CMD_DIR_OPEN
    ld      bc, 0
    call    ApiCall
    or      a
    jr      nz, Failed
    ld      hl, (PathPtr)
    ld      a, (hl)
    or      a
    jr      nz, .header
    ld      hl, RootText
.header:
    call    PrintString
    call    NewLine
.next:
    call    ReadKey
    cp      3
    jr      z, Done
    cp      27
    jr      z, Done
    ld      a, CMD_DIR_NEXT
    ld      bc, PATH_MAX + 2
    call    ApiCall
    cp      8
    jr      z, Done
    or      a
    jr      nz, Failed
    ld      a, (Reply)
    and     1
    ld      hl, DirectoryText
    jr      nz, .entry
    ld      hl, FileText
.entry:
    call    PrintString
    ld      hl, Reply + 1
    call    PrintString
    call    NewLine
    jr      .next
Done:
    call    CloseDirectory
    jp      Exit
Failed:
    push    af
    call    CloseDirectory
    pop     af
    jp      FinishStatus

UsageText:     db 'ODIR [path]',13,10,'Cartridge DSKs/directories. Esc or Ctrl-C stops.',13,10,0
RootText:      db '/',0
DirectoryText: db '<DIR> ',0
FileText:      db '      ',0
    INCLUDE 'common.inc'
    ASSERT $ < $4000
