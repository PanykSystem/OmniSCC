; OmniSCC device-based driver for Nextor 2.1.
; Based on the public Nextor DRIVER.ASM interface, Konamiman.
    DEVICE ZXSPECTRUM48
    ORG $4000
    INCLUDE "nextor_protocol.inc"
    DS $100,0
    DB "NEXTOR_DRIVER",0
    DB 1,0
    DB "OmniSCC shared flash"
    DS $4130-$," "
    JP Return
    JP DriverVersion
    JP DriverInit
    JP Unsupported
    JP Unsupported
    JP Return
    JP Return
    JP Return
    JP Return
    JP Return
    JP Return
    JP Unsupported
    DS $4160-$,0
    JP DevRW
    JP DevInfo
    JP DevStatus
    JP LunInfo
Return: RET
Unsupported: SCF
    RET
DriverVersion:
    LD A,1
    LD BC,0
    RET
DriverInit:
    OR A
    JR NZ,.allocate
    LD HL,WORK_SIZE
    OR A
    RET
.allocate:
    CALL GetWork
    PUSH IX
    POP DE
    LD HL,PollStart
    LD BC,PollEnd-PollStart
    LDIR
    XOR A
    RET
GetWork:
    XOR A
    EX AF,AF'
    XOR A
    LD IX,$4045
    CALL $4042
    LD L,(IX+0)
    LD H,(IX+1)
    PUSH HL
    POP IX
    RET
DevRW:
    PUSH AF
    CP 1
    JP NZ,InvalidDevice
    LD A,C
    CP 1
    JP NZ,InvalidDevice
    LD A,B
    OR A
    JR NZ,.begin
    POP AF
    XOR A
    RET
.begin:
    PUSH HL
    PUSH DE
    PUSH BC
    CALL GetWork
    POP BC
    POP DE
    POP HL
    LD (IX+W_LEFT),B
    LD (IX+W_PTR),L
    LD (IX+W_PTR+1),H
    LD (IX+W_CHUNK),B
    LD A,B
    CP 128
    JR NC,.invalidBuffer
    CALL CheckTransfer
    JR C,.invalidBuffer
    CALL CheckWork
    JR C,.invalidBuffer
    LD (IX+W_DONE),0
    LD (IX+W_ERR),0
    LD A,(DE)
    LD (IX+W_LBA),A
    INC DE
    LD A,(DE)
    LD (IX+W_LBA+1),A
    INC DE
    LD A,(DE)
    LD (IX+W_LBA+2),A
    INC DE
    LD A,(DE)
    LD (IX+W_LBA+3),A
    POP AF
    LD A,NC_READ
    JR NC,.command
    LD A,NC_WRITE
    JR .command
.invalidBuffer:
    POP AF
    LD A,$8B
    LD B,0
    RET
.command:
    LD (IX+W_CMD),A
    LD A,I
    LD A,0
    JP PO,.iff
    INC A
.iff:
    LD (IX+W_IFF),A
    DI
.loop:
    LD A,(IX+W_LEFT)
    CP NEXTOR_MAX_SECTORS+1
    JR C,.chunk
    LD A,NEXTOR_MAX_SECTORS
.chunk:
    LD (IX+W_CHUNK),A
    CALL CheckTransfer
    JR NC,.bufferOK
    LD (IX+W_ERR),NS_BAD_PARAM
    JP .finish
.bufferOK:
    LD A,(IX+W_CHUNK)
    LD (NP_COUNT),A
    LD A,(IX+W_LBA)
    LD (NP_LBA),A
    LD A,(IX+W_LBA+1)
    LD (NP_LBA+1),A
    LD A,(IX+W_LBA+2)
    LD (NP_LBA+2),A
    LD A,(IX+W_LBA+3)
    LD (NP_LBA+3),A
    LD A,(IX+W_CMD)
    CP NC_WRITE
    JR NZ,.issue
    LD A,(IX+W_CHUNK)
    CALL CopyData
.issue:
    PUSH IX
    POP HL
    LD A,(IX+W_CMD)
    CALL CallHL
    JR NC,.result
    OR A
    JR NZ,.result
    LD (IX+W_ERR),NS_NOT_READY
    LD C,0
    JP .validDone
.result:
    LD (IX+W_ERR),A
    LD A,(NP_DONE)
    LD C,A
    LD A,(IX+W_CHUNK)
    CP C
    JR NC,.validDone
    LD C,0
    LD (IX+W_ERR),NS_NOT_READY
.validDone:
    LD (IX+W_CHUNK),C
    LD A,(IX+W_CMD)
    CP NC_READ
    JR NZ,.advance
    LD A,C
    OR A
    CALL NZ,CopyData
.advance:
    LD C,(IX+W_CHUNK)
    LD A,(IX+W_DONE)
    ADD A,C
    LD (IX+W_DONE),A
    LD A,(IX+W_LEFT)
    SUB C
    LD (IX+W_LEFT),A
    LD A,(IX+W_LBA)
    ADD A,C
    LD (IX+W_LBA),A
    LD A,(IX+W_LBA+1)
    ADC A,0
    LD (IX+W_LBA+1),A
    LD A,(IX+W_LBA+2)
    ADC A,0
    LD (IX+W_LBA+2),A
    LD A,(IX+W_LBA+3)
    ADC A,0
    LD (IX+W_LBA+3),A
    LD L,(IX+W_PTR)
    LD H,(IX+W_PTR+1)
    LD A,C
    ADD A,A
    LD D,A
    LD E,0
    ADD HL,DE
    LD (IX+W_PTR),L
    LD (IX+W_PTR+1),H
    LD A,(IX+W_ERR)
    OR A
    JR NZ,.finish
    LD A,C
    OR A
    JR NZ,.progress
    LD (IX+W_ERR),NS_NOT_READY
    JR .finish
.progress:
    LD A,(IX+W_LEFT)
    OR A
    JP NZ,.loop
.finish:
    LD B,(IX+W_DONE)
    LD A,(IX+W_ERR)
    OR A
    JR Z,.mapped
    LD A,(IX+W_ERR)
    CP NS_BAD_PARAM
    LD A,$8B
    JR Z,.mapped
    LD A,(IX+W_ERR)
    CP NS_RANGE
    LD A,$F9
    JR Z,.mapped
    LD A,(IX+W_ERR)
    CP NS_VERIFY_ERROR
    LD A,$FA
    JR Z,.mapped
    LD A,(IX+W_ERR)
    CP NS_WRITE_ERROR
    LD A,$FE
    JR Z,.mapped
    LD A,$FC
.mapped:
    LD (IX+W_ERR),A
    LD A,(IX+W_IFF)
    OR A
    JR Z,.irqOff
    EI
.irqOff:
    LD A,(IX+W_ERR)
    OR A
    RET
InvalidDevice:
    POP AF
    LD A,$B5
    LD B,0
    RET
CheckTransfer:
    PUSH HL
    PUSH BC
    LD L,(IX+W_PTR)
    LD H,(IX+W_PTR+1)
    LD A,H
    CP $40
    JR C,.below
    CP $80
    JR C,.bad
    LD A,(IX+W_CHUNK)
    ADD A,A
    LD B,A
    LD C,0
    ADD HL,BC
    JR NC,.good
    JR .limit
.below:
    LD A,(IX+W_CHUNK)
    ADD A,A
    LD B,A
    LD C,0
    ADD HL,BC
.limit:
    LD A,H
    CP $40
    JR C,.good
    JR NZ,.bad
    LD A,L
    OR A
    JR NZ,.bad
.good:
    POP BC
    POP HL
    OR A
    RET
.bad:
    POP BC
    POP HL
    SCF
    RET
; Reject a complete caller range overlapping the allocated helper/state.
; This must run before any read chunk can overwrite its own work area.
CheckWork:
    PUSH BC
    PUSH DE
    PUSH HL
    PUSH IX
    POP HL
    LD BC,WORK_SIZE
    ADD HL,BC
    LD E,(IX+W_PTR)
    LD D,(IX+W_PTR+1)
    OR A
    SBC HL,DE
    LD A,0
    JR C,.startsAfter
    JR Z,.startsAfter
    INC A
.startsAfter:
    PUSH AF
    LD L,(IX+W_PTR)
    LD H,(IX+W_PTR+1)
    LD A,(IX+W_CHUNK)
    ADD A,A
    LD B,A
    LD C,0
    ADD HL,BC
    JR C,.wrapped
    POP AF
    OR A
    JR Z,.good
    JR .checkEnd
.wrapped:
    POP AF
    OR A
    JR NZ,.bad
.checkEnd:
    PUSH IX
    POP DE
    OR A
    SBC HL,DE
    JR C,.good
    JR Z,.good
.bad:
    POP HL
    POP DE
    POP BC
    SCF
    RET
.good:
    POP HL
    POP DE
    POP BC
    OR A
    RET
CopyData:
    ADD A,A
    LD B,A
    LD C,0
    LD L,(IX+W_PTR)
    LD H,(IX+W_PTR+1)
    LD DE,NP_BUFFER
    LD A,(IX+W_CMD)
    CP NC_WRITE
    JR Z,.copy
    EX DE,HL
.copy:
    LDIR
    RET
CallHL:
    JP (HL)
; Position-independent RAM helper; only hardware addresses are absolute.
PollStart:
    PUSH AF
    LD A,(NP_READY)
    CP NEXTOR_READY
    JR Z,.available
    POP AF
    XOR A
    SCF
    RET
.available:
    POP AF
    LD (NP_COMMAND),A
    LD BC,0
    LD DE,$0100
.loop:
    LD A,(NP_READY)
    CP NEXTOR_READY
    JR Z,.done
    DEC BC
    LD A,B
    OR C
    JR NZ,.loop
    DEC DE
    LD A,D
    OR E
    JR NZ,.loop
    XOR A
    SCF
    RET
.done:
    LD A,(NP_STATUS)
    OR A
    RET Z
    SCF
    RET
PollEnd:
W_PTR EQU PollEnd-PollStart
W_LBA EQU W_PTR+2
W_LEFT EQU W_LBA+4
W_DONE EQU W_LEFT+1
W_CHUNK EQU W_DONE+1
W_CMD EQU W_CHUNK+1
W_IFF EQU W_CMD+1
W_ERR EQU W_IFF+1
WORK_SIZE EQU W_ERR+1
    ASSERT WORK_SIZE < 128
DevInfo:
    CP 1
    JR NZ,InfoError
    LD A,B
    OR A
    JR NZ,InfoError
    LD (HL),1
    INC HL
    LD (HL),0
    XOR A
    RET
DevStatus:
    CP 1
    JR NZ,StatusError
    LD A,B
    CP 2
    JR NC,StatusError
    LD A,1
    RET
StatusError: XOR A
    RET
LunInfo:
    CP 1
    JR NZ,InfoError
    LD A,B
    CP 1
    JR NZ,InfoError
    LD (HL),0
    INC HL
    LD (HL),0
    INC HL
    LD (HL),2
    INC HL
    EX DE,HL
    LD HL,NP_CAPACITY
    LD BC,4
    LDIR
    XOR A
    LD B,5
.clear:
    LD (DE),A
    INC DE
    DJNZ .clear
    RET
InfoError: LD A,1
    RET
CodeEnd:
    ASSERT CodeEnd <= $6000
    DS $6000-$,0
Exchange:
    DS $1600,0
    DS $7F80-$,0
Parameters:
    DS 16,0
    DS $7FD0-$,0
BankTail:
    LD ($7FFF),A
    RET
    DS $8000-$,0
    SAVEBIN "nextor_driver.bin",$4000,$4000
