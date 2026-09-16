; -----------------------------------------------------------------------------
; Mailbox access, shared by every loader ROM.
;
; Included rather than copied: the poll in here is the one thing a loader cannot
; get wrong twice. Every command releases /WAIT and leaves the cartridge serving
; nothing, so the loop that waits for it has to run from MSX RAM and has to
; reject a floating bus -- and a second copy of that reasoning would drift from
; this one. The menu that hung one boot in two is what this file is for.
;
; The includer must define, with these exact meanings:
;   CART_BUF, CART_CMD, CART_STATUS, READY_SENTINEL   mailbox addresses
;   MAILRAM                                           where MailPollRom is copied
;   CMD_MAP_FILE, CMD_OPEN_READ, CMD_READ_BLOCK, CMD_CLOSE      command codes
;   RAM_INI_BUF, IniSize, StrIniFile, StrIniFileEnd   the ini buffer and its path
; and must copy MailPollRom to MAILRAM before the first command; see ExecCartCmd.
; MAILRAM must have MAILPOLL_SIZE bytes of room.
; -----------------------------------------------------------------------------

; The poll is bounded. Every command releases /WAIT, so a firmware that never
; answers leaves the Z80 spinning here with interrupts disabled: no keyboard, no
; VDP interrupt, nothing but the power switch. The bound turns that into a
; failed command, which every caller already knows how to report.
;
; Counted in poll iterations rather than in time, because there is no clock to
; read: one pass is 65536 iterations of roughly 60 T-states, so about a second
; on a 3.58 MHz Z80 and half that on a turbo machine.
;
; The bound has to clear the slowest thing a command legitimately does, and one
; command is in a class of its own: CMD_MAP_FILE copies a ROM too big for the
; RAM cache into the permanent flash region -- up to a megabyte of erase and
; program, which is tens of seconds at the flash's typical erase time and longer
; on a slow part. Everything else here is one file operation. So that command
; gets minutes and the rest get seconds; a bound that fires on healthy work
; would be worse than no bound at all.
MAILPOLL_PASSES      EQU 10       ; ~10 seconds
MAILPOLL_PASSES_LONG EQU 200      ; ~3 minutes, for CMD_MAP_FILE
MAILPOLL_SIZE        EQU 48       ; room the includer must leave at MAILRAM

; The status a timed-out command answers with. Non-zero, so every existing
; "or a / jr nz" failure path takes it, and distinct from the firmware's own
; codes so it can be told apart when one is being chased.
MAIL_TIMEOUT    EQU $FD

; =============================================================================
; PICO FILE ACCESS
; =============================================================================
; ExecCartCmd issues a single mailbox command and waits for completion.
; The caller loads any required parameters into CART_BUF before calling.
;
; Both halves run from MSX RAM, at MAILRAM, and the jump is here rather than
; after the command write for a reason: the write is what makes the firmware
; release /WAIT and stop serving, so the very next instruction fetch must
; already be coming from RAM. Getting that wrong is what hung the menu right
; after it drew its frame -- the first command it sends is the /loader.ini open,
; and the fetch after the store came from a cartridge that had stopped
; answering.
;
; MailPoll's ret returns to our own caller, so this costs the caller nothing.

ExecCartCmd:
    jp      MAILRAM

; -----------------------------------------------------------------------------
; MailPollRom is copied to MAILRAM by Init. It writes the command and polls for
; the reply, gated on two addresses so a floating bus cannot answer for the
; firmware, and bounded so a firmware that never answers cannot hang the machine.
;
;   in   A = command code
;   out  A = status, or MAIL_TIMEOUT; BC, DE, HL untouched
; -----------------------------------------------------------------------------
MailPollRom:
    DISP    MAILRAM

    ld      (CART_CMD), a
    push    bc
    push    de

    ; A still holds the command: the store above does not touch it.
    ld      d, MAILPOLL_PASSES
.passes         EQU $-1         ; the immediate above, for a test to shorten
    cp      CMD_MAP_FILE
    jr      nz, .bounded
    ld      d, MAILPOLL_PASSES_LONG

.bounded:
.pass:
    ld      bc, 0                   ; 65536 iterations per pass
.inner          EQU $-2         ; and the inner count, for the same reason
.wait:
    ld      a, (CART_STATUS)
    cp      READY_SENTINEL          ; is the cartridge answering at all?
    jr      nz, .tick
    ld      a, (CART_CMD)
    cp      READY_SENTINEL          ; same value at both = floating bus
    jr      nz, .answered

.tick:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .wait
    dec     d
    jr      nz, .pass

    ld      a, MAIL_TIMEOUT         ; gave up: the caller reports a failure

.answered:
    pop     de
    pop     bc
    ret

    ENT
MailPollRomEnd:

    ASSERT MailPollRomEnd - MailPollRom <= MAILPOLL_SIZE

LoadIniFile:
    xor     a
    ld      (IniSize), a
    ld      (IniSize+1), a

    ld      hl, RAM_INI_BUF
    ld      de, RAM_INI_BUF+1
    ld      bc, 2047
    ld      (hl), 0
    ldir                            ; clear the INI buffer

    ld      hl, StrIniFile
    ld      de, CART_BUF
    ld      bc, StrIniFileEnd-StrIniFile
    ldir                            ; filename -> CART_BUF

    ld      a, CMD_OPEN_READ
    call    ExecCartCmd
    or      a
    jr      nz, .returnZero         ; open failed

    ld      a, CMD_READ_BLOCK
    call    ExecCartCmd
    or      a
    jr      nz, .closeReturnZero    ; read failed

    ld      a, (CART_BUF)
    ld      c, a
    ld      a, (CART_BUF+1)
    ld      b, a                    ; BC = bytes read

    ld      a, b
    or      c
    jr      z, .closeReturnZero     ; empty file

    ld      hl, 2047
    or      a
    sbc     hl, bc
    jr      nc, .lengthOK

    ld      bc, 2047                ; clamp to buffer size

.lengthOK:
    ld      (IniSize), bc

    ld      hl, CART_BUF+2          ; skip 2-byte length prefix
    ld      de, RAM_INI_BUF
    ldir                            ; copy file data to INI buffer

    xor     a
    ld      (de), a                 ; ensure trailing NUL

.close:
    ld      a, CMD_CLOSE
    call    ExecCartCmd
    ld      bc, (IniSize)
    ret

.closeReturnZero:
    ld      a, CMD_CLOSE
    call    ExecCartCmd

.returnZero:
    ld      bc, 0
    ret
