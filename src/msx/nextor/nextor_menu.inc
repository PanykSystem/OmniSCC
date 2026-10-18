; Nextor action shared by the browser and favourites menus.
; HL points to a selected filename or a favourites record. Compare the last
; path component, case-insensitively, with nextor.rom and the configured name.
; Preserve HL/BC; favourites records end at NUL, comma, CR or LF.
; Browser paths are NUL-terminated and may contain commas.
IsNextorPath:
    PUSH HL
    PUSH BC
    CALL NextorBaseName
    PUSH HL
    LD DE,NextorDefault+1
    CALL NextorNameEquals
    POP HL
    JR Z,.done
    PUSH HL
    LD HL,NextorRomName
    LD A,(HL)
    OR A
    JR Z,.noConfigured
    CALL NextorBaseName
    EX DE,HL
    POP HL
    CALL NextorNameEquals
    JR .done
.noConfigured:
    POP HL
    OR 1                        ; An empty config must not match an empty file.
.done:
    POP BC
    POP HL
    RET
NextorBaseName:
    LD D,H
    LD E,L
.scan:
    LD A,(HL)
    CALL NextorNameEnd
    JR Z,.done
    INC HL
    CP '/'
    JR NZ,.scan
    LD D,H
    LD E,L
    JR .scan
.done:
    EX DE,HL
    RET
NextorNameEquals:
    LD A,(HL)
    CALL NextorNameEnd
    JR Z,.end
    CALL UpperCase
    LD C,A
    LD A,(DE)
    CALL UpperCase
    CP C
    RET NZ
    INC HL
    INC DE
    JR NextorNameEquals
.end:
    LD A,(DE)
    JP NextorNameEnd
NextorNameEnd:
    OR A
 IF !NEXTOR_BROWSER
    RET Z
    CP ','
    RET Z
    CP 13
    RET Z
    CP 10
 ENDIF
    RET
ParseNextorLine:
    LD DE,NextorKey
    LD B,10
.match:
    LD A,(DE)
    CP (HL)
    RET NZ
    INC DE
    INC HL
    DJNZ .match
.spaces:
    LD A,(HL)
    CP ' '
    JR Z,.skip
    CP 9
    JR NZ,.value
.skip:
    INC HL
    JR .spaces
.value:
    LD DE,NextorRomName
    LD B,64
.copy:
    LD A,(HL)
    OR A
    JR Z,.done
    CP 13
    JR Z,.done
    CP 10
    JR Z,.done
    LD (DE),A
    INC DE
    INC HL
    DJNZ .copy
    LD A,(HL)
    OR A
    JR Z,.done
    CP 13
    JR Z,.done
    CP 10
    JR Z,.done
    XOR A
    LD (NextorRomName),A
    LD A,1
    LD (NextorInvalid),A
    RET
.done:
    XOR A
    LD (DE),A
    LD (NextorInvalid),A
    RET
; Boot the actual selected path; CART_BUF is not readable before a command.
BootNextorPath:
    LD A,1
    LD (CountdownStop),A
    PUSH HL
    CALL KILBUF
 IF NEXTOR_BROWSER
    CALL SaveIni                 ; SaveIni uses CART_BUF: save before copying.
 ENDIF
    POP HL
    XOR A
    LD (NextorInvalid),A
    LD DE,CART_BUF
    LD B,64
.copy:
    LD A,(HL)
    CALL NextorNameEnd
    JR Z,.done
    LD (DE),A
    INC HL
    INC DE
    DJNZ .copy
    LD A,(HL)
    CALL NextorNameEnd
    JR Z,.done
    LD A,1
    LD (NextorInvalid),A
.done:
    XOR A
    LD (DE),A
    CALL RestoreBootScreen
    LD HL,NextorSwitchRom
    LD DE,SAFERAM
    LD BC,NextorSwitchEnd-NextorSwitchRom
    LDIR
    DI
    JP SAFERAM
NextorSwitchRom:
    DISP SAFERAM
    LD A,(NextorInvalid)
    OR A
    LD A,2
    JR NZ,.failed
    LD A,4
    CALL MAILRAM
    OR A
    JR NZ,.failed
    DI
    JP 0
.failed:
    CP 5
    JR C,.known
    LD A,4
.known:
    DEC A
    ADD A,A
    LD E,A
    LD D,0
    LD HL,.messages
    ADD HL,DE
    LD E,(HL)
    INC HL
    LD D,(HL)
    LD (NextorLastError),DE
    EI
    CALL INITXT
    CALL KILBUF
    LD HL,(NextorLastError)
.print:
    LD A,(HL)
    OR A
    JR Z,.wait
    PUSH HL
    CALL CHPUT
    POP HL
    INC HL
    JR .print
.wait:
    CALL CHSNS
    JR Z,.wait
    CALL CHGET
    LD A,BOOT_FAILED_MARK
    LD (BootFailed),A
    JP Init
.messages:
    DW .missing,.invalid,.unsupported,.storage
.missing: DB "NEXTOR ROM NOT FOUND",13,10,0
.invalid: DB "INVALID NEXTOR ROM/PATH",13,10,0
.unsupported: DB "NEXTOR NEEDS FAT12/FAT16",13,10,0
.storage: DB "NEXTOR STORAGE ERROR",13,10,0
    ENT
NextorSwitchEnd:
    ASSERT NextorSwitchEnd-NextorSwitchRom <= 256
NextorKey: DB "nextorrom=",0
NextorDefault: DB "/nextor.rom",0
