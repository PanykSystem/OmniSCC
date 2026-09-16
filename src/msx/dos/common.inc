; OmniSCC disk API client for MSX-DOS 1/2, sjasmplus.
; Included after each command's own entry point. Everything remains in page 0
; RAM; DOS's inter-slot routines restore the slot mapping after every access.
; No BASIC entry point or private trampoline routine is called.
;
; RDSLT: AF/BC/DE destroyed. WRSLT: AF/BC/D destroyed. Both leave DI.
; See MSX2 Technical Handbook 5.7.2 (MSX-DOS inter-slot calls).
BDOS            EQU $0005
RDSLT           EQU $000C
WRSLT           EQU $0014
EXPTBL          EQU $FCC1
CART_BUF        EQU $6000
CART_DRIVE      EQU $7E04
CART_STATUS     EQU $7FFE
CART_CMD        EQU $7FFF
CMD_CLOSE       EQU $00
CMD_CLEANUP     EQU $3A
CMD_MOUNT       EQU $3B
CMD_EJECT       EQU $3C
CMD_DIR_OPEN    EQU $3D
CMD_DIR_NEXT    EQU $3E
CMD_INFO        EQU $3F
CMD_FILE_DELETE EQU $40
CMD_FILE_COPY   EQU $41
CMD_FILE_NEW    EQU $42
CMD_FILE_STEP   EQU $43
ST_FILE_MORE   EQU $10
ST_TIMEOUT      EQU $FD         ; client-side timeout, not a firmware status
PATH_MAX        EQU 64
INFO_SIZE       EQU 2 * (3 + PATH_MAX + 1)

; Parse exactly one optional argument from the length-delimited DOS command
; tail. Quotes may enclose the path or the whole argument, including A:/B:.
; Carry -> usage (also /?). ArgLength includes a drive prefix, if present;
; PathPtr excludes it. Paths are NUL-terminated and bounded before any I/O.
ParseArgument:
    xor     a
    ld      (Drive), a
    ld      (Quoted), a
    ld      (ArgLength), a
    ld      hl, Argument
    ld      (PathPtr), hl
    ld      a, ($0080)
    cp      128
    jp      nc, .bad
    ld      b, a
    ld      hl, $0081
    ld      de, Argument
    ld      c, 0
.skip:
    ld      a, b
    or      a
    jr      z, .end
    ld      a, (hl)
    cp      ' '
    jr      z, .space
    cp      9
    jr      nz, .token
.space:
    inc     hl
    djnz    .skip
    jr      .end
.token:
    ld      a, b
    or      a
    jr      z, .end
    ld      a, (hl)
    inc     hl
    dec     b
    cp      '"'
    jr      z, .quote
    push    af
    ld      a, (Quoted)
    or      a
    jr      nz, .literal
    pop     af
    cp      ' '
    jr      z, .trailing
    cp      9
    jr      z, .trailing
    jr      .copy
.literal:
    pop     af
.copy:
    cp      32
    jr      c, .bad
    ld      (de), a
    inc     de
    inc     c
    ld      a, c
    cp      PATH_MAX + 3          ; up to A: plus 64 path bytes
    jr      nc, .bad
    jr      .token
.quote:
    ld      a, (Quoted)
    xor     1
    ld      (Quoted), a
    jr      .token
.trailing:
    ld      a, b
    or      a
    jr      z, .end
    ld      a, (hl)
    inc     hl
    dec     b
    cp      ' '
    jr      z, .trailing
    cp      9
    jr      z, .trailing
    jr      .bad
.end:
    xor     a
    ld      (de), a
    ld      a, (Quoted)
    or      a
    jr      nz, .bad
    ld      a, c
    ld      (ArgLength), a
    ld      hl, Argument
    cp      2
    jr      c, .path
    ld      a, (hl)
    inc     hl
    ld      b, (hl)
    dec     hl
    cp      '/'
    jr      nz, .prefix
    ld      a, b
    cp      '?'
    jr      z, .bad
.prefix:
    ld      a, b
    cp      ':'
    jr      nz, .path
    ld      a, (hl)
    and     $DF
    sub     'A'
    cp      2
    jr      nc, .bad
    ld      (Drive), a
    inc     hl
    inc     hl
    dec     c
    dec     c
.path:
    ld      a, c
    cp      PATH_MAX + 1
    jr      nc, .bad
    ld      (PathPtr), hl
    ; No embedded FAT drive prefix (or DOS drive other than A/B).
.check:
    ld      a, (hl)
    or      a
    ret     z
    cp      ':'
    jr      z, .bad
    inc     hl
    jr      .check
.bad:
    scf
    ret

; Read-only scan of primary slots and, where EXPTBL says expanded, subslots.
; The existing firmware has no identification command. Its published BASIC
; vector is 7800h and its DSKIO/DSKCHG JPs target adjacent RAM trampolines.
; Check all of these before writing. Never read the INIT vector at 4002/3:
; that would resemble a boot scan and re-arm the firmware's borrowed ROM.
FindCartridge:
    ld      c, 0
.primary:
    ld      b, 1
    ld      e, c
    ld      d, 0
    ld      hl, EXPTBL
    add     hl, de
    ld      a, (hl)
    and     $80
    jr      z, .plain
    ld      b, 4
.plain:
    or      c
    ld      (CartSlot), a
.subslot:
    call    ProbeSlot
    jr      z, .found
    ld      a, (CartSlot)
    add     a, 4
    ld      (CartSlot), a
    djnz    .subslot
    inc     c
    ld      a, c
    cp      4
    jr      nz, .primary
    ei
    scf
    ret
.found:
    ei
    or      a
    ret

ProbeSlot:
    push    bc
    ld      hl, $4000
    call    SlotRead
    cp      'A'
    jr      nz, .done
    inc     hl
    call    SlotRead
    cp      'B'
    jr      nz, .done
    ld      hl, $4004
    call    SlotRead
    or      a
    jr      nz, .done
    inc     hl
    call    SlotRead
    cp      $78
    jr      nz, .done
    ld      hl, $4010
    call    SlotRead
    cp      $C3
    jr      nz, .done
    inc     hl
    call    SlotRead
    ld      e, a
    inc     hl
    call    SlotRead
    ld      d, a
    cp      $C0
    jr      c, .reject
    cp      $F4                  ; below the system work area
    jr      nc, .reject
    inc     de
    inc     de
    inc     de
    inc     hl
    call    SlotRead
    cp      $C3
    jr      nz, .done
    inc     hl
    call    SlotRead
    cp      e
    jr      nz, .done
    inc     hl
    call    SlotRead
    cp      d
.done:
    pop     bc
    ret
.reject:
    ld      a, 1
    or      a
    jr      .done

; HL address -> A value; preserve BC/DE/HL, flags follow RDSLT.
SlotRead:
    push    bc
    push    de
    ld      a, (CartSlot)
    call    RDSLT
    pop     de
    pop     bc
    ret

; HL address, E value; preserve BC/DE/HL, destroy AF.
SlotWrite:
    push    bc
    push    de
    ld      a, (CartSlot)
    call    WRSLT
    pop     de
    pop     bc
    ret

; Leave DI through the following ApiCall, so another disk request cannot
; overwrite the shared parameters between preparation and command dispatch.
WritePath:
    di
    ld      hl, (PathPtr)
    ld      bc, CART_BUF
.loop:
    ld      e, (hl)
    inc     hl
    push    hl
    ld      h, b
    ld      l, c
    call    SlotWrite
    pop     hl
    inc     bc
    ld      a, e
    or      a
    jr      nz, .loop
    ret

WriteDrive:
    di
    ld      a, (Drive)
    ld      e, a
    ld      hl, CART_DRIVE
    jp      SlotWrite

; A command, BC bytes to copy on success (0, 66 or 136).
; A result, carry only on client timeout. Reply always lives in our own RAM.
; Buffer is closed BEFORE EI or any DOS call. 00h does not close the directory
; iterator, so ODIR can print between NEXT calls without obscuring the ROM.
ApiCall:
    di
    ld      (ReplyLength), bc
    ld      e, a
    ld      hl, CART_CMD
    call    SlotWrite
    call    WaitReply
    jr      c, .timeout
    push    af
    or      a
    jr      nz, .close
    ld      bc, (ReplyLength)
    ld      a, b
    or      c
    jr      z, .close
    ld      hl, CART_BUF
    ld      de, Reply
.copy:
    call    SlotRead
    ld      (de), a
    inc     hl
    inc     de
    dec     bc
    ld      a, b
    or      c
    jr      nz, .copy
.close:
    ld      hl, CART_CMD
    ld      e, CMD_CLOSE
    call    SlotWrite            ; synchronous /WAIT-held command
    pop     af
    or      a
    ei
    ret
.timeout:
    ; Do not send a second command into firmware still working on the first.
    ld      a, 1
    ld      (TimedOut), a
    ld      a, ST_TIMEOUT
    ei
    scf
    ret

; A floating bus can also read A5h: BOTH registers must match the disk
; protocol (different from the loader API's reversed register arrangement).
; Bound the wait to 65536 inter-slot polls; even a dead cartridge returns DOS.
WaitReply:
    ld      bc, 0
.loop:
    ld      hl, CART_CMD
    call    SlotRead
    cp      $A5
    jr      nz, .tick
    dec     hl
    call    SlotRead
    cp      $A5
    jr      nz, .ready
.tick:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .loop
    scf
    ret
.ready:
    or      a
    ret

CloseDirectory:
    ld      a, (TimedOut)
    or      a
    ret     nz
    di
    ld      hl, CART_CMD
    ld      e, CMD_CLEANUP
    call    SlotWrite
    ei
    ret

; Function 06h deliberately avoids DOS's automatic Ctrl-C termination while
; a directory handle is live. ODIR polls Ctrl-C/Esc itself and closes it.
; It also prints '$' literally (paths are not BDOS 09h strings).
PrintChar:
    push    af
    push    bc
    push    de
    push    hl
    ld      e, a
    ld      c, 6
    call    BDOS
    pop     hl
    pop     de
    pop     bc
    pop     af
    ret

PrintString:
    ld      a, (hl)
    inc     hl
    or      a
    ret     z
    call    PrintChar
    jr      PrintString

NewLine:
    ld      a, 13
    call    PrintChar
    ld      a, 10
    jp      PrintChar

ReadKey:
    push    bc
    push    de
    push    hl
    ld      e, $FF
    ld      c, 6
    call    BDOS
    pop     hl
    pop     de
    pop     bc
    or      a
    ret

; HL unsigned integer, decimal without leading zeroes.
PrintNumber:
    ld      bc, -10000
    call    .digit
    ld      bc, -1000
    call    .digit
    ld      bc, -100
    call    .digit
    ld      bc, -10
    call    .digit
    ld      a, l
    add     a, '0'
    call    PrintChar
    xor     a
    ld      (NumberStarted), a
    ret
.digit:
    ld      e, '0' - 1
.again:
    inc     e
    add     hl, bc
    jr      c, .again
    or      a
    sbc     hl, bc
    ld      a, (NumberStarted)
    or      a
    jr      nz, .emit
    ld      a, e
    cp      '0'
    ret     z
.emit:
    ld      a, 1
    ld      (NumberStarted), a
    ld      a, e
    jp      PrintChar

Usage:
    ld      hl, UsageText
    call    PrintString
    jp      Exit
NoCartridge:
    ld      hl, NoCartText
    call    PrintString
    jp      Exit
FinishStatus:
    or      a
    jp      z, Exit
    cp      6
    ld      hl, NoFileText
    jr      z, .print
    cp      2
    ld      hl, NoDriveText
    jr      z, .print
    cp      $12
    ld      hl, ExistsText
    jr      z, .print
    cp      $14
    ld      hl, InUseText
    jr      z, .print
    cp      $16
    ld      hl, InvalidText
    jr      z, .print
    cp      $18
    ld      hl, FullText
    jr      z, .print
    cp      $0A
    ld      hl, WriteProtectedText
    jr      z, .print
    cp      ST_TIMEOUT
    ld      hl, TimeoutText
    jr      z, .print
    ld      hl, ErrorText
.print:
    call    PrintString
Exit:
    ei
    jp      0                   ; DOS 1 warm boot / DOS 2 terminate

ExistsText:    db 'File exists',13,10,0
InUseText:     db 'Image in use: EJECT it first',13,10,0
InvalidText:   db 'Invalid argument',13,10,0
FullText:      db 'Disk full',13,10,0
WriteProtectedText: db 'Write protected',13,10,0
NoCartText:     db 'OmniSCC disk interface not found',13,10,0
NoFileText:     db 'File or directory not found',13,10,0
NoDriveText:    db 'No such drive',13,10,0
TimeoutText:    db 'OmniSCC timeout',13,10,0
ErrorText:      db 'OmniSCC API error',13,10,0
CartSlot:      db 0
Drive:         db 0
Quoted:        db 0
ArgLength:     db 0
TimedOut:      db 0
NumberStarted: db 0
PathPtr:       dw Argument
ReplyLength:   dw 0
Argument:      ds PATH_MAX + 3, 0
Reply:         ds INFO_SIZE + 1, 0
