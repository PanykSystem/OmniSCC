; Shared ROM-resident failure display. Only entered after the RAM poll has
; received a failure, when the original loader is serving the cartridge again.
; A is the mount result; see include/MountResult.hxx. Never read CART_BUF here:
; its overlay is closed. Paths are retained in the menu's own RAM instead.
MountFailed:
    push    af
    ei
    call    INITXT
    call    KILBUF
    pop     af
    push    af
    ld      hl, MountUnknownMsg
    cp      MAIL_TIMEOUT
    jr      nz, .notTimeout
    ld      hl, MountTimeoutMsg
    jr      .reason
.notTimeout:
    sub     $20
    IFDEF ROM_DATABASE_MENU
    cp      13
    ELSE
    cp      10
    ENDIF
    jr      nc, .reason
    add     a, a
    ld      e, a
    ld      d, 0
    ld      hl, MountReasonTable
    add     hl, de
    ld      e, (hl)
    inc     hl
    ld      d, (hl)
    ex      de, hl
.reason:
    call    MountPrintLine
    pop     af
    cp      $29                     ; read error in the Disk ROM dump
    jr      z, .diskRom
    cp      $24
    jr      c, .image
    cp      $28
    jr      nc, .image
.diskRom:
    ld      hl, DiskRomName
    ld      a, (hl)
    or      a
    jr      nz, .path
    ld      hl, MountDefaultDiskRom
    jr      .path
.image:
    IF MOUNT_BROWSER
    ld      hl, LastPath
    ELSE
    ld      hl, (MountPath)
    ENDIF
    IF !MOUNT_BROWSER
    call    MountPrintRecord
    jr      .prompt
    ENDIF
.path:
    call    MountPrintLine
.prompt:
    ld      hl, MountReturnMsg
    call    MountPrintLine
.wait:
    call    CHSNS
    jr      z, .wait
    call    CHGET
    ld      a, BOOT_FAILED_MARK
    ld      (BootFailed), a
    jp      Init

MountPrintLine:
    ld      b, 0
    jr      MountPrintRecord.loop
MountPrintRecord:
    ld      b, 1
.loop:
    ld      a, (hl)
    or      a
    jr      z, .end
    cp      13
    jr      z, .end
    cp      10
    jr      z, .end
    IF !MOUNT_BROWSER
    cp      ','                     ; only raw favourite records have labels
    jr      nz, .character
    ld      a, b
    or      a
    jr      nz, .end
    ld      a, ','
.character:
    ENDIF
    push    hl
    push    bc
    call    CHPUT
    pop     bc
    pop     hl
    inc     hl
    jr      .loop
.end:
    ld      a, 13
    call    CHPUT
    ld      a, 10
    jp      CHPUT

MountReasonTable:
    dw MountRomMissingMsg, MountDiskMissingMsg, MountRomSizeMsg, MountDiskSizeMsg
    dw MountDiskRomMissingMsg, MountDiskRomSizeMsg, MountUnsupportedMsg, MountPatchMsg
    dw MountReadMsg, MountReadMsg
    IFDEF ROM_DATABASE_MENU
    dw MountMapperMsg, MountSaveSpaceMsg, MountSavePrepareMsg
    ENDIF
MountRomMissingMsg:     db "ROM FILE NOT FOUND", 0
MountDiskMissingMsg:    db "DISK IMAGE NOT FOUND", 0
MountRomSizeMsg:        db "INVALID ROM SIZE", 0
MountDiskSizeMsg:       db "INVALID DISK IMAGE SIZE", 0
MountDiskRomMissingMsg: db "DISK ROM FILE NOT FOUND", 0
MountDiskRomSizeMsg:    db "INVALID DISK ROM SIZE", 0
MountUnsupportedMsg:   db "UNSUPPORTED DISK ROM", 0
MountPatchMsg:         db "DISK ROM PATCH FAILED", 0
    IFDEF ROM_DATABASE_MENU
MountMapperMsg:        db "UNSUPPORTED ROM MAPPER", 0
MountSaveSpaceMsg:     db "NOT ENOUGH SPACE FOR KUC SAVE", 0
MountSavePrepareMsg:   db "KUC SAVE PREPARATION FAILED", 0
    ENDIF
MountReadMsg:          db "FILE READ ERROR", 0
MountTimeoutMsg:       db "CARTRIDGE RESPONSE TIMEOUT", 0
MountUnknownMsg:       db "MOUNT FAILED: UNKNOWN ERROR", 0
MountReturnMsg:        db "PRESS ANY KEY TO RETURN TO MENU", 0
MountDefaultDiskRom:    db "disk.rom", 0

; The firmware serves no cartridge bytes while a handler runs. A timeout is
; therefore different from a returned failure: even the display must live in
; RAM. Plant it before sending the mount, clear of the switch stub and mailbox.
TIMEOUTRAM EQU SAFERAM + $80
PrepareMountTimeout:
    push    hl
    push    de
    push    bc
    ld      hl, MountTimeoutRom
    ld      de, TIMEOUTRAM
    ld      bc, MountTimeoutEnd-MountTimeoutRom
    ldir
    pop     bc
    pop     de
    pop     hl
    ret
MountTimeoutRom:
    DISP TIMEOUTRAM
MountTimeoutRAM:
    ei
    call    INITXT
    call    KILBUF
    ld      hl, .reason
    call    .print
    IF MOUNT_BROWSER
    ld      hl, LastPath
    ELSE
    ld      hl, (MountPath)
    ENDIF
    call    .print
    ld      hl, .restart
    call    .print
.hang:
    halt
    jr      .hang
.print:
    ld      a, (hl)
    or      a
    jr      z, .end
    cp      13
    jr      z, .end
    cp      10
    jr      z, .end
    IF !MOUNT_BROWSER
    cp      ','
    jr      z, .end
    ENDIF
    push    hl
    call    CHPUT
    pop     hl
    inc     hl
    jr      .print
.end:
    ld      a, 13
    call    CHPUT
    ld      a, 10
    jp      CHPUT
.reason:  db "CARTRIDGE RESPONSE TIMEOUT", 0
.restart: db "PLEASE RESTART THE MSX", 0
    ENT
MountTimeoutEnd:
    ASSERT TIMEOUTRAM + MountTimeoutEnd-MountTimeoutRom <= $CB00
