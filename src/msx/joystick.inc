; Joystick input for both menus. Poll once per VBlank, before auto-boot.
; BIOS calls preserve no scratch registers here: the caller keeps BC/DE/HL.
; JoystickState reserves five bytes: direction key, repeat timer, held triggers,
; current direction key, current triggers. Trigger bits remain per physical
; button, so pressing a second controller works even while the first is held.
; See MSX2 Technical Handbook, chapter 5, GTSTCK / GTTRIG.
InitJoystick:
    xor     a
    ld      hl, JoystickState
    ld      b, 5
.clear:
    ld      (hl), a
    inc     hl
    djnz    .clear
    ret

PollJoystick:
    push    bc
    push    de
    push    hl
    ld      a, 1
    call    $00D5                   ; GTSTCK: port 1 wins simultaneous directions
    or      a
    jr      nz, .direction
    ld      a, 2
    call    $00D5
.direction:
    ld      hl, JoystickDirections
    ld      e, a
    ld      d, 0
    add     hl, de
    ld      a, (hl)
    ld      (JoystickState+3), a

    ld      b, 1                    ; GTTRIG 1/2 = A, 3/4 = B, on ports 1/2
    ld      c, 1
    ld      d, 0
.buttons:
    ld      a, b
    push    bc
    push    de
    call    $00D8
    pop     de
    pop     bc
    or      a
    jr      z, .nextButton
    ld      a, d
    or      c
    ld      d, a
.nextButton:
    sla     c
    inc     b
    ld      a, b
    cp      5
    jr      nz, .buttons
    ld      a, d
    ld      (JoystickState+4), a
    ld      a, (JoystickState+3)
    or      d
    jr      z, .repeat
    ld      a, 1
    ld      (CountdownStop), a       ; held input also cancels auto-boot
.repeat:
    ld      a, (JoystickState+3)
    ld      c, a
    or      a
    jr      z, .released
    ld      a, (JoystickState)
    cp      c
    jr      nz, .newDirection
    ld      a, (JoystickState+1)
    dec     a
    ld      (JoystickState+1), a
    jr      nz, .noDirection
    ld      a, 6                    ; repeat every 6 frames after first delay
    ld      (JoystickState+1), a
    jr      .edges
.newDirection:
    ld      a, c
    ld      (JoystickState), a
    ld      a, 20                   ; initial delay: 20 frames (0.33-0.4 s)
    ld      (JoystickState+1), a
    jr      .edges
.released:
    xor     a
    ld      (JoystickState), a
    ld      (JoystickState+1), a
.noDirection:
    ld      c, 0
.edges:
    ld      a, (JoystickState+2)
    xor     d
    and     d                       ; newly pressed physical buttons only
    ld      e, a
    ld      a, d
    ld      (JoystickState+2), a
    ld      a, e
    and     3
    jr      nz, .enter
    ld      a, e
    and     12
    jr      nz, .back
    ld      a, c                    ; cursor event, or zero
    jr      .done
.enter:
    ld      a, 13
    jr      .done
.back:
    ld      a, 8
.done:
    pop     hl
    pop     de
    pop     bc
    or      a
    ret

; Diagonals select their vertical component; horizontal means page navigation.
JoystickDirections:
    db      0, 30, 30, 28, 31, 31, 31, 29, 30
