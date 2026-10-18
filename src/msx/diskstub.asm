; -----------------------------------------------------------------------------
; OmniSCC disk driver stub.
;
; Four blobs come out of this file, selected by the STUB_PART define:
;   STUB_PART = 0  ROM part: the INIT prologue and the INIT tail it copies
;                  into MSX RAM. Written over the borrow window of the patched
;                  Disk ROM image and handed back before the vendor INIT runs
;                  -- see CRomPatcher::Apply.
;   STUB_PART = 1  RAM part: the trampoline, copied into MSX RAM reserved
;                  below HIMEM by the INIT tail. As small as it can be made:
;                  every byte of it is a byte MSX BASIC never gets back.
;   STUB_PART = 2  WINDOW part: the dispatcher proper, which the firmware
;                  serves out of the cartridge window at WIN_BASE instead of
;                  MSX RAM. See the WINDOW PART banner for why that is safe.
;   STUB_PART = 3  BASIC part: the Disk BASIC statement handler, served the
;                  same way out of a second window at BAS_BASE. BASIC's CALSLT
;                  enters it straight from the ROM header vector at 0x4004.
;
; Parts 0 and 1 are assembled twice at different origins by build_stub.py,
; which diffs the outputs to derive a relocation table. Do not add absolute
; addresses that are not meant to be relocated -- MSX system variables and BIOS
; entry points are constants, not labels, so they are naturally excluded from
; the diff. Parts 2 and 3 are assembled at their own fixed origins and must
; come out identical at any STUB_ORG, which build_stub.py checks the same way.
; -----------------------------------------------------------------------------
    DEVICE NOSLOT64K

    IFNDEF STUB_ORG
STUB_ORG        EQU $8000
    ENDIF
    IFNDEF STUB_PART
STUB_PART       EQU 0
    ENDIF

; --- MSX system variables and BIOS entries (constants, never relocated) ------
MEMSIZ          EQU $F672       ; end of BASIC string space
STKTOP          EQU $F674       ; saved BASIC stack top, used by boot sectors
SAVSTK          EQU $F6B1       ; valid saved stack bottom
FILTAB          EQU $F860       ; BASIC FCB pointer table
NULBUF          EQU $F862       ; system file buffer
HIMEM           EQU $FC4A       ; top of free RAM
CART_BUF        EQU $6000       ; cartridge exchange buffer
CART_CMD        EQU $7FFF       ; cartridge command register / ready flag
CART_STATUS     EQU $7FFE       ; command status, readable while one is ready
RSLREG          EQU $0138       ; read primary slot register
ENASLT          EQU $0024       ; select a slot into a page
EXPTBL          EQU $FCC1       ; slot expansion table
SLTTBL          EQU $FCC5       ; subslot mirror, EXPTBL + 4
RAMAD1          EQU $F342       ; slot of RAM in page 1

; The name of the CALL statement BASIC is dispatching, uppercased and
; NUL-terminated. It is how the extension is asked "is this one of yours?", and
; the BASIC part answers by comparing it against its own table.
PROCNM          EQU $FD89

; Print one character. In page 0, so the BASIC part can call it with the window
; still open: page 1 stays ours and nothing has to be switched. That is the
; whole reason a statement can report an error at all -- raising a real BASIC
; error would mean CALBAS, which switches page 1 out from under code that is
; executing there.
CHPUT           EQU $00A2

; Read one row of the keyboard matrix: A = row in, the row's keys out, a zero
; bit per key held down. CALL ODIR's paging pause is what uses it.
;
; SNSMAT and not CHGET, which is the obvious way to wait for a key and is the
; wrong one here: CHGET reads the buffer the VDP interrupt fills, this handler
; runs under di between BIOS calls, and the interrupt that fills that buffer
; gets in only when a BIOS call's own ei lets a tick through -- not on the
; schedule the buffer is written against -- so CHGET would wait with the
; keyboard apparently dead. SNSMAT drives the PPI itself and needs no
; interrupts. Like CHPUT it is in page 0, so the window stays open across the
; call and page 1 stays ours.
SNSMAT          EQU $0141

; Rows in that matrix. Eleven: the nine every MSX has, plus the two a numeric
; keypad adds -- a machine without one answers them as nothing pressed, so
; scanning all eleven costs two SNSMAT calls and needs no test for the model.
KEY_ROWS        EQU 11

; The two hooks the BIOS's interrupt handler calls, five bytes each in MSX RAM:
; H.KEYI first, on every interrupt, and H.TIMI once the VDP has been identified
; as the source. Both are a ret on a stock machine; a Disk ROM's INIENV turns
; H.TIMI into a CALLF to its motor-off timer, and on both supported dumps that
; timer lives INSIDE the BASIC window -- 0x7837 in the National image, 0x7827
; in the Yamaha one. The BASIC part writes a ret over the first byte of each
; for the length of a statement and puts the byte back on the way out: see
; BasEntry for why a di is not enough.
H_KEYI          EQU $FD9A
H_TIMI          EQU $FD9F

; --- Mailbox commands --------------------------------------------------------
; CMD_CLOSE_BUFFER closes the exchange-buffer overlay. It is only ever opened
; by CMD_DISK_READ and CMD_DISK_STATUS below (DiskCart.cxx's Execute() clears
; return_buffer at the top of every 0x7FFF write, so the *next* command closes
; it too, but that command can be thousands of instructions away, or never --
; nothing here served ROM through 0x6000-0x7FFE otherwise). DoDskio and
; DoDskchg each send it explicitly once they are done reading the buffer, so
; the overlay never outlives the command that opened it.
CMD_CLOSE_BUFFER EQU $00
CMD_DISK_READ   EQU $31
CMD_DISK_WRITE  EQU $32
CMD_DISK_STATUS EQU $33
CMD_DISK_FLUSH  EQU $34
CMD_DISK_SETRAM EQU $35
CMD_DISK_FORMAT EQU $39
CMD_DISK_SELECT EQU $44

; CMD_DISK_WRITE and CMD_DISK_FLUSH are the two commands the firmware may not
; serve with /WAIT held, and they are issued through MailCmd in the trampoline
; rather than through ExecCmd in the window because of it. Programming one
; 512-byte page is most of a millisecond, every eighth page erases a 4 KB flash
; sector, and persisting the filesystem's own sector map erases another -- tens
; of milliseconds each, against the ~2 ms in which a stalled Z80 stops
; refreshing MSX DRAM. See MailCmd, and CmdDiskWrite in DiskCart.cxx.
;
; Writes need no command to open the exchange buffer: buffer-range writes are
; captured whatever the overlay is doing, so the driver fills it and then sends
; the command. CMD_DISK_FLUSH goes once per DSKIO call, not once per chunk.

; Status codes the firmware answers with. Only ST_WRPROT has to be named here:
; it is the one status the driver turns into a different DSKIO error code (0,
; "write protected") instead of the generic write fault, because it is the only
; one the caller can do something about.
ST_WRPROT       EQU $0A

; And this one, because an empty drive is a different DSKIO error from a data
; error: the firmware answers it for a drive with no image mounted, which is
; drive B whenever loader.ini has no disk2= line.
ST_NOTREADY     EQU $02

; The path named no file the volume could open. Its own status because it is
; the one outcome of CALL OMOUNT a user can do something about -- a typo in a
; name -- and the statement says so in as many words. DISK_ST_NOFILE in
; DiskCart.hxx must agree, which test_constants.py checks.
ST_NOFILE       EQU $06

; The directory has no more entries. Its own status because it is the one way a
; listing ends normally: everything else nonzero is the cartridge failing
; mid-listing and gets a line of text, while this one just stops the loop. The
; firmware closes its directory handle as it answers this, so the listing owes
; it nothing afterwards. DISK_ST_ENDDIR in DiskCart.hxx must agree, which
; test_constants.py checks.
ST_ENDDIR       EQU $08

; Not a firmware status at all: what MailCmd answers when the firmware never
; does. Each caller already turns anything it does not recognise into its own
; generic failure -- a write fault for DSKIO, "other error" for DSKFMT -- so
; this needs no mapping of its own, only a value that is neither success nor one
; of the firmware's.
ST_TIMEOUT      EQU $0C

; How long MailCmd waits. Counted in poll iterations because there is no clock
; to read: one pass is 65536 iterations of roughly 60 T-states, so about a
; second on a 3.58 MHz Z80 and half that on a turbo machine.
;
; The commands this poll waits for are the three that touch flash -- write,
; flush, format -- and the two that open or close a file, CALL OMOUNT and CALL
; OEJECT. The slowest is a format: a dozen sector programs and the erases
; the log-structured allocator does every eighth page, which is a second or two
; even on a slow part. Ten passes is several times that, which is the point: a
; bound that fires on healthy work would be worse than no bound.
MAILCMD_PASSES  EQU 10

; Open and close the code window. While it is open the firmware answers reads
; of WIN_BASE..WIN_BASE+WIN_SIZE-1 out of the exchange buffer, where the
; dispatcher blob lives, instead of out of the Disk ROM image. The trampoline
; opens it on the way into a disk call and closes it on the way out, so the
; vendor bytes underneath are hidden only while our own code is the only thing
; running -- see the WINDOW PART banner. Neither command blocks: each sets a
; flag with /WAIT held, so the write itself is the whole handshake and there is
; nothing to poll for.
CMD_DISK_WINDOW_ON  EQU $37
CMD_DISK_WINDOW_OFF EQU $38

; And the BASIC window's close. There is no matching open, which is the whole
; difference between the two windows: BASIC reaches the statement handler
; through CALSLT, not through a command write, so there is no chance to ask for
; the window first and the bus loop opens it itself on the entry fetch. Only
; the close is ours to send, and it can only be sent from MSX RAM -- the
; instruction fetched after this write already comes from the vendor's bytes.
; See BasicClose, and case 0x3A in DiskCart.cxx. test_constants.py ties this
; number to that case.
CMD_BASIC_WINDOW_OFF EQU $3A

; CALL OMOUNT and CALL OEJECT. Both are file I/O on the firmware's side --
; Mount() opens a file and builds a CLMT, Unmount() closes one and may flush it
; -- so like the three flash commands they are answered with /WAIT released and
; polled for through MailCmd from MSX RAM, never from a window. See
; CmdBasicMount in DiskCart.cxx.
;
; The path goes at the base of the exchange buffer, NUL-terminated, and the
; drive in P_DRIVE. Buffer-range writes are captured whatever the overlay is
; doing, so nothing has to be opened first.
CMD_BASIC_MOUNT EQU $3B
CMD_BASIC_EJECT EQU $3C

; CALL ODIR, which is a pair because a listing is an unbounded number of round
; trips: 0x3D opens a directory on the cartridge's own FAT volume and 0x3E
; steps it, one entry per command, until the firmware answers ST_ENDDIR. The
; handle lives between them on the firmware's side, which is what makes them a
; pair rather than one command -- see CmdBasicDirOpen in DiskCart.cxx for where
; it is closed.
;
; Both are file I/O, so both are answered with /WAIT released and polled for
; through MailCmd from MSX RAM, exactly like the mount and the eject above:
; opening a path walks its components and a step scans directory sectors past
; the hidden entries, and neither is bounded in microseconds the way a capped
; sector read is.
;
; 0x3D takes the path at the base of the exchange buffer, NUL-terminated and
; empty for the volume's root. 0x3E answers with one entry in the same place:
; an attribute byte, bit 0 set for a directory, then the name with its NUL.
CMD_BASIC_DIR_OPEN EQU $3D
CMD_BASIC_DIR_NEXT EQU $3E

; CALL ODISK. Unlike the two above this one opens nothing: it reads four
; accessors off the two CDiskImages the cartridge already holds, so it is
; answered on the fast path with /WAIT still asserted, like CMD_DISK_STATUS.
; It is still issued through MailCmd rather than a poll of this blob's own --
; MailCmd is already resolved here for the other two, and a second poll body in
; the window would cost bytes to say the same thing more narrowly.
;
; The reply goes at the base of the exchange buffer, one record per drive in
; drive order: a state byte (bit 0 mounted, bit 1 write-protected), the sector
; count as a little-endian word, and the path with its NUL. An empty drive
; still gets all three, because the driver walks a record to reach the next one.
; See CmdBasicInfo in DiskCart.cxx.
CMD_BASIC_INFO  EQU $3F
CMD_FILE_DELETE EQU $40
CMD_FILE_COPY   EQU $41
CMD_FILE_NEW    EQU $42
CMD_FILE_STEP   EQU $43
ST_FILE_MORE   EQU $10

; Longest path CMD_BASIC_MOUNT accepts, not counting the terminator. The
; firmware copies exactly this many bytes at most (DISK_BASIC_PATH_MAX in
; DiskCart.hxx, which test_constants.py ties this to), so a longer one could
; only ever be truncated -- and a silently truncated path is what once turned a
; 21-character diskrom= into a 15-character one that opened a different dump.
; The parser refuses the statement instead.
BAS_PATH_MAX    EQU 64

; CALL ODIR's line, in characters: a marker field that holds "<DIR> " or the
; same width in blanks, then the name, cut at DIR_NAME_MAX. One entry to a
; line, 38 characters at most, which fits SCREEN 0's 40-column line.
;
; Thirty-two for the name because the firmware sends an entry's long name when
; it is at most this long and the 8.3 alias when it is not -- the rule the
; loader's browser applies to its own listing -- so on the cartridge nothing
; that arrives is ever cut, and what is printed can always be typed back into
; CALL OMOUNT. The cut exists for the emulator, which has no alias to fall back
; to and sends the long name whatever its length. DISK_DIR_NAME_MAX in
; DiskCart.hxx is the firmware's side of the rule; test_constants.py ties the
; two together.
DIR_NAME_MAX    EQU 32
DIR_MARK_COLS   EQU 6

; Lines per page before the listing pauses for a key. An MSX text screen is 24
; rows, and stopping two short of it leaves the row the listing resumes on and
; the one BASIC's "Ok" takes. The path header a listing opens with is the first
; line of its first page.
;
; It counts the lines this prints, not the rows they take: a line is 38
; columns at most, which is one row on a 40-column screen and, for a name too
; long for SCREEN 1's 32, two there. A page of long names can then run past the
; top of a SCREEN 1 screen, which is the price of a count that does not ask the
; VDP how wide the line is.
DIR_PAGE_LINES  EQU 22

; While a command's result is ready, CART_CMD answers with this token and
; CART_STATUS answers with the status. Both answers repeat for as long as the
; result stands, so the poll below survives a missed or duplicated bus cycle.
;
; The token also has to be distinguishable from a bus that nobody is driving.
; While a slow handler runs, the firmware releases /WAIT and stops driving the
; bus, which then floats at its last driven value -- and that value is the last
; sector byte the previous transfer read out, which for binary data is 0xA5 as
; often as any other byte. A one-address poll cannot tell that stale 0xA5 from a
; real one and returns early onto a buffer nobody filled. The poll instead reads
; both addresses and requires them to DIFFER the way only a driven bus can: an
; undriven bus reads the same value at every address, so if CART_STATUS reads
; 0xA5 too, the bus is floating rather than answering. No status the firmware
; sends is 0xA5 -- its set is 00/02/04/06/08/0A/FE, which test_constants.py
; resolves from DiskCart.cxx and checks -- so a genuine reply always clears the
; second test. ST_TIMEOUT (0C) is this driver's own, never sent by the
; firmware, and is kept clear of the token for the same reason.
CMD_READY_TOKEN EQU $A5

; CMD_DISK_UNBORROW gives the borrowed ROM bytes back -- see InitTail and
; CRomPatcher::Apply. It is issued from the INIT tail once that tail is running
; in MSX RAM, never from the ROM part: the bytes underneath the ROM part are
; exactly what the handler rewrites.
CMD_DISK_UNBORROW EQU $36

; Parameter block inside the exchange buffer. Shared by both parts: the INIT
; tail uses P_SETRAM_BASE and the handshake below, the dispatcher uses the
; rest.
P_SETRAM_BASE   EQU CART_BUF + $1E00
P_LBA_LO        EQU CART_BUF + $1E00
P_LBA_MID       EQU CART_BUF + $1E01
P_LBA_HI        EQU CART_BUF + $1E02
P_COUNT         EQU CART_BUF + $1E03
P_DRIVE         EQU CART_BUF + $1E04
P_FLAGS         EQU CART_BUF + $1E05    ; CMD_DISK_STATUS reply, bit 0 = changed
P_MAGIC0        EQU CART_BUF + $1E06
P_MAGIC1        EQU CART_BUF + $1E07

; Handshake for CMD_DISK_SETRAM. The INIT tail writes this word into the
; parameter block immediately before issuing the command; CmdDiskSetRam
; refuses a 0x35 that arrives without it and clears it once used, so the
; command is one-shot per handshake rather than one-shot per boot.
;
; The reordering that moved our allocation to *after* the vendor INIT is what
; makes this necessary. Vendor INIT code writes to what it believes are its
; own FDC registers, and on plenty of interfaces 0x7FFF is one of them -- so a
; stray 0x35 can now reach the command register before the tail's real one. A
; plain one-shot flag would then have spent itself on the stray write and
; refused the only command that mattered, silently.
SETRAM_MAGIC0   EQU $A5
SETRAM_MAGIC1   EQU $5A

; Fixed MSX RAM the INIT tail runs from.
;
; The tail cannot run from the borrow window: the window has to go back to the
; vendor before the vendor INIT is called, because the vendor executes code
; inside it (a boot trace shows 408F..40B7 running as ordinary Disk BASIC).
; And it cannot run from the area reserved below HIMEM, because reserving that
; area is the whole point of the tail.
;
; Page-3 RAM exists on every MSX, and during cartridge INIT its lower half
; holds nothing: the BIOS has already sized RAM and put its work area at the
; top of page 3, BASIC has not cold-started, and a Disk ROM's own buffers are
; allocated downward from HIMEM or at hard-coded addresses just under it.
;
; Not 0xC000, which is where the boot sector is loaded and 0xC01E where it is
; entered -- the National dump does exactly that from 5AE5/5B00. That happens
; well after every cartridge INIT has returned, so the tail would in fact be
; dead by then, but a stub that shares an address with the boot sector is a
; trap for whoever reads this next.
;
; loader.asm makes the same bet at 0xCA00 for the stubs that outlive a ROM
; unmap. This sits between the two, clear of both.
SAFERAM_INIT    EQU $C800

; Exact assembled size of the RAM part (STUB_PART=1) below. The INIT tail
; copies exactly this many bytes out of the exchange buffer, where
; CmdDiskSetRam leaves the blob already relocated for the address it is going
; to (see DiskCart.cxx). build_stub.py asserts this equals the RAM part's
; actual assembled length on every build -- update it there if it grows.
RAM_STUB_LEN    EQU 405

; Reserved MSX RAM layout, relative to the base the INIT tail picks:
;   +0                       the dispatcher blob
;   +SLOT_BYTE_OFS           our own slot id, written by the tail
;
; Every byte of this reservation is a byte MSX BASIC does not get. That is not
; an abstract cost -- it is what broke the first disk this cartridge was tried
; with. BASIC derives STKTOP, MEMSIZ and its string space from HIMEM, so
; lowering HIMEM by N moves all of them down by N; a disk whose BLOAD image
; ends close under the untouched STKTOP then lands on BASIC's stack instead.
; With the original 1025-byte reservation, SCCMUZAX.DSK's image (C800..D8FF)
; overlapped STKTOP (D796) by 361 bytes and the machine wandered off into
; unwritten RAM. So this figure is the smallest that works, not a round number.
;
; There is no bounce buffer here any more: a page-1 transfer stages its bytes
; on the stack, which is in page 3 and therefore addressable in both slot
; configurations (see CopyOut's .viaBounce). That path is the only thing the
; buffer was for, and it ran only on the disks that take it, while the 32
; reserved bytes came out of BASIC on every disk.
; Offset of the dispatcher inside the reserved area. Zero, and named rather
; than written as a literal because DiskCart.cxx, the openMSX device and
; docs/omniscc_api.md all have to agree with it; test_constants.py checks that.
DISP_OFFSET     EQU 0
DISP_SIZE       EQU RAM_STUB_LEN

; The dispatcher's jump table is exactly the six slots the MSX standard
; defines, which is what CRomPatcher::SetDispatcher writes. test_constants.py
; ties this to DBE_COUNT.
DISP_ENTRIES    EQU 6
SLOT_BYTE_OFS   EQU DISP_OFFSET + DISP_SIZE

; The BASIC extension's two exits, as offsets from the trampoline's base.
;
; They are offsets rather than labels because the blob that jumps to them is a
; different part of this file: it is served at BAS_BASE and assembled there, so
; it cannot see a label that the INIT tail places at an address chosen from the
; HIMEM the vendor INIT left behind. What the blob can do is read that base out
; of the patched DSKIO thunk and add a constant -- so the constant has to exist
; on both sides, and the RAM part ASSERTs that its own labels really do land
; here.
;
; Immediately after the six-entry jump table, because that is the one part of
; the trampoline's layout already fixed from outside (CRomPatcher::SetDispatcher
; points the patched ROM entries at base + 3 * entry). Following it costs
; nothing, where a seventh and eighth table slot would have cost six bytes of
; the reservation -- six bytes MSX BASIC never gets back.
BAS_RET_OFS     EQU DISP_ENTRIES * 3
BAS_CHAIN_OFS   EQU BAS_RET_OFS + 3

; And MailCmd, for the same reason and with the same answer. CALL OMOUNT and
; CALL OEJECT are answered with /WAIT released, so the loop that polls for them
; may not be fetched from a window: for the length of one of those commands the
; cartridge serves no bus at all. The dispatcher in the other window is handed
; MailCmd's address in IY by the trampoline that calls it; nothing calls this
; blob, so it computes the address the same way it computes the two exits.
;
; Immediately after them -- BasicChain's scf plus the seven bytes of BasicClose
; -- which is why MailCmd is placed where it is in the RAM part rather than
; after the entries that use it. The ASSERT there is what catches an edit to
; those two that moves it.
BAS_MAIL_OFS    EQU BAS_CHAIN_OFS + 8

; And the OrigStatementAddr cell, which is the trampoline's last two bytes --
; the same "end of the blob" CmdDiskSetRam addresses it by. build_stub.py's
; guard check is what makes addressing it that way safe from either side.
BAS_ORIG_OFS    EQU RAM_STUB_LEN - 2

; Where the blob reads the trampoline's base from.
;
; Nothing hands it over. The dispatcher in the code window is called by the
; trampoline, so it gets BounceCopy and MailCmd in IX and IY; BASIC's CALSLT
; enters the BASIC window straight from the ROM header vector at 0x4004, with
; no code of ours ahead of it to have loaded anything. The patched jump table
; is the one copy of the address that page 1 can read: CRomPatcher::Apply
; rewrote 0x4010 as "JP nn" and SetDispatcher filled the operand with
; base + 3 * DBE_DSKIO, which for DSKIO is the base itself. That entry is never
; absent -- Validate refuses a dump whose 0x4010 is not a JP into its own
; image, and Apply claims every present entry but GETDPB. test_rompatcher.cxx
; pins both halves of that: DSKIO's operand is at 0x4011, and SetDispatcher
; fills it with the base itself.
;
; It reads zero only if CMD_DISK_SETRAM was refused, which is the same state in
; which the patched jump table still points every disk call at address 0: the
; driver has nowhere to live and there is no honest recovery from here either,
; so this is documented rather than guarded. See the INIT tail's .noRam.
DISKROM_DSKIO   EQU $4010
TRAMP_BASE_PTR  EQU DISKROM_DSKIO + 1

; Padding above our data, reserved and then left alone. Zero, and here as the
; escape hatch rather than as a mechanism.
;
; A Disk ROM may claim its work area at hard-coded addresses instead of
; through HIMEM. The Yamaha YIS805 image does exactly that: a loop at 57BD
; zeroes F1C9..F37F with both bounds written into the ROM as constants, and it
; reads HIMEM only for a second block of buffers lower down.
;
; While we allocated *before* the vendor INIT, a pad was the only defence, and
; it was a bad one: 512 bytes moved our data clear of F1C9 and then the sixth
; of the vendor's own allocations asked for 0x7104 bytes instead of 0x0C02 and
; the boot died with "No enough memory". The size came out of a DPB the vendor
; had not filled in yet, so the pad was really a lottery over uninitialised
; memory -- one value passed by luck, the next did not.
;
; Allocating after the vendor INIT removes the whole question: HIMEM by then
; already accounts for everything the vendor took, hard-coded areas included.
; Raise this only if some dump turns out to write below its own HIMEM after
; INIT has returned.
VENDOR_PAD      EQU 0
RESERVE_SIZE    EQU SLOT_BYTE_OFS + 1 + VENDOR_PAD

; Bytes of sector data staged on the stack per slot-switch pair by CopyOut's
; page-1 path. It replaces the buffer that used to be reserved below HIMEM for
; the same job: the stack is in page 3, so it is addressable both while the
; cartridge owns page 1 and while the destination's RAM slot does, and it costs
; nothing on the disks that never take that path. Must be even (the staging
; loops move words) and small enough to be transient stack: a hop uses
; HOP_SIZE bytes of stack plus whatever ENASLT needs underneath it.
;
; Every bounce hop is exactly this size: a chunk is a whole number of 512-byte
; sectors and 512 is a multiple of HOP_SIZE, so the loop below never has to
; handle a short final hop.
HOP_SIZE        EQU 32

; --- the code windows -------------------------------------------------------
; The BASIC extension blob's window. Below the dispatcher's, because the space
; above it belongs to the parameter block and to the vendor's controller
; registers -- National maps those at 0x7FB8 and Yamaha at 0x7F80, and the read
; path answers them separately.
;
; The published entry stays at BAS_ENTRY (7800h); new file helpers occupy
; the preceding sector. The firmware loads the blob at BAS_BASE and opens
; the entire window on a fetch of BAS_ENTRY. Constants match DiskCart.hxx.
BAS_BASE        EQU $7600
BAS_ENTRY       EQU $7800
BAS_SIZE        EQU 1536

; Where the firmware serves the dispatcher blob from while the window is open,
; and how much of the exchange buffer that costs. The window sits above the
; BASIC window and below the parameter block, and the sector data stops below
; both, so a read command (at most MAX_SECTORS sectors) can never write over
; the code being executed.
;
; DiskCart.hxx has to agree: DISK_CODE_START, DISK_CODE_END and
; DISK_MAX_SECTORS, which test_constants.py checks.
WIN_BASE        EQU $7C00
WIN_SIZE        EQU 512

; Sector-data capacity of the exchange buffer, in sectors: everything from
; CART_BUF up to the LOWER of the two windows. This is what the buffer holds,
; not what one command asks for -- see CHUNK_SECTORS.
;
; Three sectors hold BASIC code, leaving eleven for data. Expanding the
; window changes chunk size only; the 301-byte MSX RAM reservation is unchanged.
MAX_SECTORS     EQU (BAS_BASE - CART_BUF) / 512

; The window's own entry points, at fixed addresses because the blob is
; assembled at WIN_BASE and never moves. The trampoline calls these.
WIN_DSKIO       EQU WIN_BASE + 0
WIN_DSKCHG      EQU WIN_BASE + 3

; The INIT tail copies RAM_STUB_LEN bytes to the reserved base, and the byte
; at base + SLOT_BYTE_OFS has to survive that. Without this, a RAM part grown
; past DISP_SIZE walks over the slot byte and then out of the reserved area
; into the stack -- silently, since build_stub.py only checks RAM_STUB_LEN
; against the blob's own length.
    ASSERT RAM_STUB_LEN <= DISP_SIZE

    IF STUB_PART = 2
    ; Fixed: the firmware serves this blob at WIN_BASE and nothing relocates
    ; it, so STUB_ORG is deliberately ignored here.
    ORG WIN_BASE
    ELSEIF STUB_PART = 3
    ; Fixed window. The ROM header points at BAS_ENTRY inside the blob;
    ; loading begins at BAS_BASE. Both are independent of STUB_ORG.
    ORG BAS_BASE
    ELSE
    ORG STUB_ORG
    ENDIF

    IF STUB_PART = 0
; =============================================================================
; ROM PART -- INIT prologue, and the INIT tail it plants in MSX RAM
;
; The BIOS calls InitPrologue instead of the vendor INIT, because
; CRomPatcher::Apply redirected the header vector at 0x4002 here.
;
; The prologue itself does almost nothing: it copies the tail into fixed MSX
; RAM, hands it the vendor INIT address and jumps. Everything else happens in
; the tail, running from RAM, in this order:
;
;   1. give the borrow window back                     CMD_DISK_UNBORROW
;   2. call the vendor INIT
;   3. reserve RAM below the HIMEM the vendor left behind
;   4. tell the firmware where that RAM is             CMD_DISK_SETRAM
;   5. copy the dispatcher there, record our slot
;   6. return to the BIOS
;
; Steps 1 and 2 coming before step 3 is the whole reason this file is shaped
; this way. A Disk ROM's INIT assumes every byte below its own HIMEM is free:
; it allocates downward from HIMEM, it zeroes work areas at hard-coded
; addresses near the top of RAM, and at least one of them sizes a later
; allocation out of a DPB it has not filled in yet. Reserving before it ran
; corrupted that arithmetic, and both dumps died at the same allocator with
; "No enough memory" -- for one padding value out of two, which made the pad a
; lottery over uninitialised memory rather than a fix. Reserving after it has
; run is the ordinary thing a second cartridge does and needs no guesses about
; the vendor's layout at all.
;
; Step 1 has to come before step 2 as well: the vendor executes code inside the
; borrow window -- a boot trace shows 408F..40B7 running as ordinary Disk BASIC
; -- so the window cannot still be ours when the vendor INIT is entered. That
; is what forces the tail into RAM: after step 1 there is no stub left in ROM
; to run from.
;
; What that buys, beyond correctness: no code in this file polls the mailbox
; from cartridge ROM any more. The prologue issues no commands at all, and
; every command the tail issues is polled from page 3.
; =============================================================================
InitPrologue:
    di

    ; Plant the tail. It is assembled with DISP at SAFERAM_INIT, so this is a
    ; straight copy to a fixed address and the jump below needs no relocation.
    ld      hl, TailImage
    ld      de, SAFERAM_INIT
    ld      bc, TailEnd - TailStart
    ldir

    ; Hand the tail the vendor INIT address. It cannot read OrigInitAddr for
    ; itself: that cell lives in the borrow window, and by the time the tail
    ; wants it the window has already gone back to the vendor.
    ld      hl, (OrigInitAddr)
    ld      (SAFERAM_INIT + VendorInit - TailStart), hl

    jp      SAFERAM_INIT

; -----------------------------------------------------------------------------
; INIT tail. Emitted here as part of the ROM blob, executed from SAFERAM_INIT.
;
; Nothing inside the DISP block is a relocation site: build_stub.py derives the
; table by diffing two assemblies at different origins, and displaced labels
; are the same in both.
; -----------------------------------------------------------------------------
TailImage:
    DISP    SAFERAM_INIT
TailStart:

    ; 1. Give the borrow window back, before a single vendor instruction runs.
    ; From here the served image is byte-identical to the dump apart from the
    ; jump table thunks and the INIT vector.
    ld      a, CMD_DISK_UNBORROW
    call    TailCmd

    ; 2. Run the vendor INIT. Interrupts go back on across it: the BIOS calls
    ; cartridge INIT with them enabled, and a vendor INIT that waits on JIFFY
    ; or a VDP frame hangs forever without them. Page 1 is still selected to
    ; us, so a plain call into cartridge ROM works, and the vendor's own ret
    ; comes back here rather than going straight to the BIOS.
    ei
    ld      hl, .fromVendor
    push    hl
    ld      hl, (VendorInit)
    jp      (hl)
.fromVendor:
    di

    ; 3. Move the stack down before reserving anything, and carry its contents
    ; with it.
    ;
    ; The MSX stack sits directly below HIMEM, and by now it is several calls
    ; deep -- boot loop, slot scan, CALSLT -- so the bytes just under HIMEM
    ; are live return addresses, including the one this tail eventually
    ; returns through. The vendor INIT left it that way too: it lowers HIMEM
    ; and moves the stack with it. Reserving there
    ; without moving SP puts the slot byte at HIMEM-1, over the high byte of
    ; the outermost one: the machine finishes the whole slot scan and then
    ; returns into garbage, which is about as hard to trace back to here as a
    ; failure gets. The vendor ROM does exactly this dance for its own work
    ; area (National dump, 5EB8: lower HIMEM; 5F14: pop hl / ld sp,hl / ldir
    ; the old stack down), and the standard requires it of any INIT routine
    ; that lowers HIMEM.
    ;
    ; Follow the Disk BASIC ALLOC contract, including its saved pointers.
    ; HIMEM alone is insufficient: boot sectors such as Aleste 2 restore SP
    ; from STKTOP, while DOS puts its RAM slot helpers below the new HIMEM.
    ; Leaving STKTOP behind makes their first CALLF overwrite those helpers.
    ld      hl, 0
    add     hl, sp
    ex      de, hl                  ; DE = old SP
    ld      hl, (STKTOP)
    or      a
    sbc     hl, de
    inc     hl                      ; include the byte at STKTOP, as ALLOC does
    ld      b, h
    ld      c, l
    ld      hl, -RESERVE_SIZE
    add     hl, de
    ld      sp, hl
    ex      de, hl                  ; HL = source, DE = lowered stack
    ldir

    ld      bc, -RESERVE_SIZE
    ld      hl, (HIMEM)
    add     hl, bc
    ld      (HIMEM), hl
    ld      de, -2*(2+9+256)         ; two BASIC FCBs, matching vendor ALLOC
    add     hl, de
    ld      (FILTAB), hl
    ex      de, hl                  ; DE = relocated FCB table
    ld      hl, (MEMSIZ)
    add     hl, bc
    ld      (MEMSIZ), hl
    ld      hl, (NULBUF)
    add     hl, bc
    ld      (NULBUF), hl
    ld      hl, (STKTOP)
    add     hl, bc
    ld      (STKTOP), hl
    dec     hl
    dec     hl
    ld      (SAVSTK), hl

    ; Rebuild closed FCBs rather than retaining pointers into their old area.
    ; This is the same initialization used by Disk BASIC's ALLOC/CLRFCB.
    ld      l, e                    ; HL = DE = FILTAB
    ld      h, d
    inc     hl
    inc     hl
    inc     hl
    inc     hl
    ld      a, 2
.fcb:
    ex      de, hl
    ld      (hl), e
    inc     hl
    ld      (hl), d
    inc     hl
    ex      de, hl
    ld      bc, 7
    ld      (hl), b
    add     hl, bc
    ld      (hl), b
    ld      bc, 9+256-7
    add     hl, bc
    dec     a
    jr      nz, .fcb

    ; 4. Hand the address to the firmware, behind the handshake. It rewrites
    ; the six jump table operands in its own copy of the ROM image and leaves
    ; the dispatcher blob, relocated for that address, at the base of the
    ; exchange buffer.
    ld      (P_SETRAM_BASE), hl
    ld      a, SETRAM_MAGIC0
    ld      (P_MAGIC0), a
    ld      a, SETRAM_MAGIC1
    ld      (P_MAGIC1), a
    ld      a, CMD_DISK_SETRAM
    call    TailCmd
    or      a
    jr      nz, .noRam

    ; 5. Copy the dispatcher into our area, which starts at the reserved base,
    ; and close the overlay. The source is the exchange buffer, in page 1; this
    ; code is in page 3, so the overlay being open cannot answer an
    ; instruction fetch with sector data -- the hazard that kept the old
    ; ROM-resident prologue reading its dispatcher out of the ROM image
    ; instead.
    ld      de, (HIMEM)             ; already lowered: DE = dispatcher destination
    ld      hl, CART_BUF
    ld      bc, RAM_STUB_LEN
    ldir
    ld      a, CMD_CLOSE_BUFFER
    call    TailCmd

    ; Work out which slot we are in and hand it to the dispatcher at the top of
    ; the reserved area, past both the bounce buffer and the dispatcher itself.
    ; It cannot go through a scratch byte in the ROM part: the firmware's bus
    ; loop only captures writes to the command register and the exchange buffer
    ; (0x7FFF and 0x6000-0x7FFE), and a write anywhere else in this cartridge's
    ; ROM window is silently dropped, exactly like real ROM. Page 1 is still
    ; selected to us -- CALSLT put us here and the vendor INIT gave the page
    ; back the way it found it -- so RSLREG/EXPTBL/SLTTBL read our own slot.
    ;
    ; Slot id format is E000SSPP: PP primary, SS subslot, E expanded flag.
    call    RSLREG                  ; A = primary slot register
    rrca
    rrca                            ; page 1 selection down into bits 0-1
    and     $03
    ld      c, a                    ; C = 0000_00PP
    ld      b, 0
    ld      hl, EXPTBL
    add     hl, bc
    ld      a, (hl)
    and     $80                     ; E: is this primary slot expanded?
    ; Test the E flag immediately: "or c" below sets its own zero flag from
    ; the merged result, and C already holds nonzero PP bits whenever we are
    ; not in primary slot 0, which would clobber a zero test done afterwards.
    jr      z, .notExpanded         ; not expanded: C already holds 0000_00PP
    or      c
    ld      c, a                    ; C = E000_00PP
    inc     hl
    inc     hl
    inc     hl
    inc     hl                      ; HL -> SLTTBL entry (SLTTBL = EXPTBL + 4)
    ld      a, (hl)
    and     $0C                     ; page 1 subslot is already in bits 2-3
    or      c
    ld      c, a
.notExpanded:
    ld      a, c
    ld      hl, (HIMEM)             ; already lowered: HL = reserved base
    ld      de, SLOT_BYTE_OFS
    add     hl, de
    ld      (hl), a

    ; 6. Back to the BIOS, which is where the vendor INIT's ret would have gone
    ; if we had not intercepted it.
    ;
    ; .noRam is the same exit taken when SETRAM was refused. Nothing is unwound
    ; there: HIMEM stays lowered over an area nothing will use, and the patched
    ; jump table entries stay at their JP 0000 placeholders, so the first disk
    ; call would jump to zero. There is no honest recovery from that point --
    ; the driver has nowhere to live -- and the refusal itself means vendor
    ; code forged the handshake, which nothing observed has ever done. The
    ; crumb is the diagnosis.
.noRam:
    ei
    ret

; -----------------------------------------------------------------------------
; TailCmd is ExecCmd for the INIT tail, which runs from page 3 before the
; dispatcher exists. Same contract, same reason for the two reads.
; -----------------------------------------------------------------------------
TailCmd:
    ; One mailbox command, start to finish.
    ;   in   A = command code
    ;   out  A = status, 0 on success
    ;
    ; Two addresses, and neither read changes anything on the cartridge side.
    ;
    ; While a slow handler runs the firmware releases /WAIT and stops driving
    ; the bus. It has to: holding /WAIT for milliseconds stalls the Z80, a
    ; stalled Z80 issues no refresh cycles, and MSX DRAM decays. An undriven
    ; MSX data bus does not read back as 0xFF -- it floats at its last driven
    ; value, which is the last sector byte the previous transfer read out. For
    ; binary data that is 0xA5 as often as any other byte, so a poll that waits
    ; for CART_CMD == CMD_READY_TOKEN alone can match a floating bus and return
    ; early onto a buffer nobody filled. That is the whole failure: it scales
    ; with the number of reads and looks random because it depends on the data.
    ;
    ; The way out is that a floating bus is monochromatic -- nothing drives it,
    ; so every address reads the same held value -- while a driven bus is not.
    ; So the poll reads BOTH registers and requires them to differ the way only
    ; a driven bus can: CART_CMD must read the token AND CART_STATUS must read
    ; something else. A floating bus reads the token at both, and no real status
    ; is 0xA5 (00/02/04/FE), so the second test passes exactly when the cartridge
    ; is genuinely answering. Both reads repeat until the next command, so a read
    ; lost to a missed cycle just comes round again.
    ld      (CART_CMD), a
.wait:
    ld      a, (CART_CMD)
    cp      CMD_READY_TOKEN         ; token at 0x7FFF?
    jr      nz, .wait
    ld      a, (CART_STATUS)        ; status at 0x7FFE
    cp      CMD_READY_TOKEN         ; same value here means the bus is floating,
    jr      z, .wait                ; not answering -- no real status is 0xA5
    ret

    ; The vendor INIT address, written here by the prologue before it jumps.
VendorInit:
    dw      0
TailEnd:
    ENT

    ; The blob ends with a two-byte guard and the OrigInitAddr cell.
    ; CRomPatcher::Apply patches the cell once it knows where the borrow window
    ; landed, addressing it as the blob's last two bytes; the guard is what
    ; build_stub.py checks on every build so that patch cannot drift onto
    ; something else. Neither is a relocation site -- the value comes from the
    ; firmware, not from this blob's assembly origin -- and nothing can fall
    ; through into them, the tail image above being data as far as ROM
    ; execution is concerned.
    db      $5A, $A5
OrigInitAddr:
    dw      0

    ENDIF

    IF STUB_PART = 1

; =============================================================================
; RAM PART -- trampoline
;
; The only part of the driver that lives in MSX RAM, and it is small on purpose:
; the INIT tail reserves it below HIMEM for the whole session, BASIC derives
; STKTOP, MEMSIZ and its string space from HIMEM, and a disk whose BLOAD image
; ends just under BASIC's stack is destroyed by the difference.
;
; Three jobs, and each one is here because it cannot be in the window. It opens
; the code window around each disk call and closes it again, because the
; dispatcher it calls into is served through page 1 and the vendor bytes
; underneath must be visible the rest of the time. It owns the page-1 transfer
; path: reaching a page-1 address means handing page 1 to the RAM slot, and the
; window would go with it. And it owns the poll for the commands the firmware
; answers with /WAIT released -- writes -- because for the length of one of
; those the cartridge serves no bus at all, so the poll's own instruction
; fetches have to come from somewhere else.
;
; The jump table must stay first and in this order: CRomPatcher::SetDispatcher
; points the patched ROM entries at base + 3 * entry.
; =============================================================================
TrampTable:
    jp      TrampDskio
    jp      TrampDskchg
    jp      TrampGetdpb
    jp      TrampChoice
    jp      TrampDskfmt
    jp      TrampMtoff

; -----------------------------------------------------------------------------
; The BASIC extension's two ways out, and why so little of them is here.
;
; Closing the BASIC window is a single write to CART_CMD, and the instruction
; fetched AFTER that write already comes from the vendor's bytes: the firmware
; answers BAS_BASE..BAS_BASE+BAS_SIZE-1 out of the exchange buffer only while
; the window is open. So the close cannot execute from the window it closes,
; and that -- not convenience -- is what puts this handful of instructions in
; MSX RAM.
;
; Only this handful, though. Everything the statement handler decides before it
; leaves is decided in the blob, where it costs bytes of the exchange buffer
; instead of bytes of the reservation, and the reservation is the one thing
; here MSX BASIC pays for on every disk.
;
;   BasicRet    returns to BASIC with carry clear: the statement was ours.
;   BasicChain  leaves with carry set, which is how BASIC's dispatcher is told
;               the name was not ours. Where the ret below lands is decided by
;               the blob: for a dump that published a statement handler of its
;               own the blob has pushed that address, so the handler is entered
;               with HL and PROCNM exactly as BASIC left them -- that is what
;               keeps CALL FORMAT and CALL SYSTEM working -- and for a dump
;               that published none the blob has pushed nothing, so the ret
;               goes back to BASIC and the name is refused rather than jumped
;               to address 0.
;
;               So the caller's stack IS the argument: pushing the vendor's
;               address before jumping here is how a name is chained, and
;               pushing nothing is how one is refused. A caller that meant to
;               chain and forgot the push returns to BASIC instead, with carry
;               set -- which BASIC reports as a syntax error, so CALL FORMAT
;               simply stops existing and nothing says why.
;
; Nothing in BasicClose touches the flags, so the carry each entry sets on its
; way through survives the close and reaches BASIC.
;
; Immediately after the jump table, and at the offsets BAS_RET_OFS and
; BAS_CHAIN_OFS name: the blob computes both from the trampoline's base and
; cannot see these labels. The ASSERTs are what keep the two sides honest --
; get them wrong and BASIC's first CALL jumps into the middle of TrampDskio.
;
; MailCmd follows them for the same reason: the blob calls it for the two slow
; statement commands and has to find it by offset too. Nothing else in this
; part is at a fixed offset, so keeping the three together is what makes the
; third offset a small constant rather than a running total of everything above
; it.
; -----------------------------------------------------------------------------
BasicRet:
    or      a                       ; carry clear: the statement was ours
    jr      BasicClose
BasicChain:
    scf                             ; carry set: the name was not ours
BasicClose:
    ld      a, CMD_BASIC_WINDOW_OFF
    ld      (CART_CMD), a
    ei
    ret

    ASSERT BasicRet - TrampTable == BAS_RET_OFS
    ASSERT BasicChain - TrampTable == BAS_CHAIN_OFS

; -----------------------------------------------------------------------------
; MailCmd issues one mailbox command and polls for its result, from MSX RAM.
;
;   in   A = command code
;   out  A = status, 0 on success; BC, DE, HL, IX, IY preserved
;
; ExecCmd in the window does the same job and every fast command uses it. This
; copy exists for the commands the firmware cannot answer with /WAIT held --
; the three that program or erase flash, and the two BASIC statement commands
; that open or close a file. For the length of one of those the firmware is
; inside its handler and drives nothing, so an instruction fetch from the code
; window would read an undriven bus and the Z80 would execute whatever the bus
; happened to be holding. That is not a theory: it is what a /WAIT-releasing
; read path did on hardware, crashing once per command, and it is why reads now
; hold the line instead.
;
; Both windows are covered by that: CALL OMOUNT is fetched from the BASIC window
; at BAS_BASE rather than the dispatcher's at WIN_BASE, and the firmware stops
; answering either one just the same.
;
; This loop is in MSX RAM, so the handler cannot starve its fetches, and the
; Z80 keeps issuing M1 cycles throughout -- which is the other half of the
; point, because those cycles are what refreshes MSX DRAM while the flash write
; runs. Its *reads* still land on the unserved cartridge, and the two-address
; test is what makes that safe: until the handler finishes, CART_CMD reads a
; floating bus, which answers the same value at CART_STATUS as well.
; -----------------------------------------------------------------------------
; The poll is bounded, and this is the one that has to be. Every command it
; waits for is answered with /WAIT released, so a firmware that dies inside one
; leaves the Z80 spinning here with interrupts disabled: no keyboard, no VDP
; interrupt, nothing but the power switch, in the middle of somebody's disk
; write. With a bound it becomes a disk error, and the machine is still there to
; take the next keystroke.
;
; The /WAIT-held polls -- ExecCmd and TailCmd -- are deliberately left unbounded.
; A bound cannot help them: their own instruction fetches come out of the
; cartridge window, so a firmware that stops serving stops them dead long before
; any counter runs out, and one that is still serving has already answered.
MailCmd:
    ld      (CART_CMD), a
    push    bc
    push    de
    ld      d, MAILCMD_PASSES
.passes         EQU $-1         ; the immediate above, for a test to shorten

.pass:
    ld      bc, 0                   ; 65536 iterations per pass
.inner          EQU $-2         ; and the inner count, for the same reason
.wait:
    ld      a, (CART_CMD)
    cp      CMD_READY_TOKEN         ; token at 0x7FFF?
    jr      nz, .tick
    ld      a, (CART_STATUS)        ; status at 0x7FFE
    cp      CMD_READY_TOKEN         ; same value here means a floating bus
    jr      nz, .answered

.tick:
    dec     bc
    ld      a, b
    or      c
    jr      nz, .wait
    dec     d
    jr      nz, .pass

    ld      a, ST_TIMEOUT

.answered:
    pop     de
    pop     bc
    ret

    ASSERT MailCmd - TrampTable == BAS_MAIL_OFS

; -----------------------------------------------------------------------------
; DSKIO and DSKCHG both run in the window. Neither entry may disturb what the
; caller passed in -- DSKIO takes the drive in A and the direction in carry --
; so opening the window goes through WindowOn, which puts AF back.
;
; Interrupts go off here rather than inside the window: an interrupt taken
; between opening the window and entering it would run with vendor bytes hidden.
; MSX-DOS always calls with interrupts enabled, so the ei on the way out
; restores the caller's state rather than assuming it.
; -----------------------------------------------------------------------------
TrampDskio:
    di
    call    DiskShortcut
    call    WindowOn
    ld      ix, BounceCopy          ; the window calls this for page-1 chunks
    ld      iy, MailCmd             ; ... and this for the slow commands
    call    WIN_DSKIO
    jr      TrampExit

TrampDskchg:
    di
    call    DiskShortcut
    call    WindowOn
    call    WIN_DSKCHG
TrampExit:
    ; A and carry carry the result, B the sectors transferred. Closing the
    ; window needs A, so both go through the stack.
    push    af
    push    bc
    ld      a, CMD_DISK_WINDOW_OFF
    ld      (CART_CMD), a
    pop     bc
    pop     af
    ei
    ret

WindowOn:
    push    af
    ld      a, CMD_DISK_WINDOW_ON
    ld      (CART_CMD), a
    pop     af
    ret

; A single held digit 1..9 selects a discovered DSK member in drive A before this disk
; call. Read the PPI directly: games may disable BIOS keyboard scanning or
; replace the timer hook. No BIOS call here may re-enable interrupts. Preserve
; port C's upper outputs (tape/click) and original keyboard row on every path.
; Only an unambiguous single digit is accepted. Scan on both disk entry points:
; a game doing raw sector reads need not call DSKCHG first.
;
; This lives in RAM and runs BEFORE WindowOn. Selecting an image opens files,
; syncs pending writes and rebuilds its sector map with /WAIT released, so the
; entire command/poll must run outside the unserved cartridge windows.
DiskShortcut:
    push    af
    or      a
    jr      nz, .exit               ; drive A only
    push    bc
    push    de
    push    hl
    ld      c, 0
    in      a, ($AA)
    ld      d, a
    ld      a, d
    and     $F0                    ; row 0: digits 0..7
    out     ($AA), a
    in      a, ($A9)
    cpl
    bit     0, a                   ; 0 is not a disk; reject 0+another digit too
    jr      nz, .restore
    and     $FE                    ; ignore 0
    ld      l, a
    ld      a, d
    and     $F0
    or      1                      ; row 1: digits 8 and 9 in bits 0 and 1
    out     ($AA), a
    in      a, ($A9)
    cpl
    and     3
    ld      h, a
    srl     h
    rr      l                      ; drop bit 0 before scanning digits 1..9
    ld      b, 9
.scan:
    srl     h
    rr      l
    jr      nc, .next
    ld      a, c
    or      a
    jr      nz, .ambiguous
    ld      a, 10
    sub     b
    ld      c, a
.next:
    djnz    .scan
    jr      .restore
.ambiguous:
    ld      c, 0
.restore:
    ld      a, d
    out     ($AA), a
    ld      a, c
    or      a
    jr      z, .done
    ld      (P_COUNT), a           ; number, 1..9
    xor     a
    ld      (P_DRIVE), a
    ld      a, CMD_DISK_SELECT
    call    MailCmd
    ; A failed selection leaves the old disk inserted. Continue the original
    ; disk call with its original arguments; never consume the changed flag.
.done:
    pop     hl
    pop     de
    pop     bc
.exit:
    pop     af
    ret

; -----------------------------------------------------------------------------
; The entries that need no code window stay here. DSKFMT is one of them: all it
; does is check its arguments and send one command, and the parameter block it
; writes is captured whatever the overlay is doing, so there is nothing the
; dispatcher would add.
;
; CHOICE answers HL = 0: no menu to show, which is the truth here -- the
; image's size is fixed, so the only format on offer is the one its own size
; implies.
;
; It used to return a pointer to an empty string in this trampoline, and that
; HUNG Disk BASIC's CALL FORMAT right after it asked for the drive letter. The
; reason is in the vendor code, which is byte-identical in both dumps at 0x6113:
;
;   6113  ld a,l / or h / jr z,613E    ; HL = 0: skip the menu, format anyway
;   6117  pop af / push af             ; A = the interface's slot
;   6119  call 000C                    ; RDSLT -- read the string FROM THE SLOT
;   611C  and a / jr z,6126            ; ...until a NUL
;   611F  call 408F / inc hl / jr      ; print it
;
; The string is read with RDSLT in the cartridge's own slot, so it has to live
; where the cartridge answers -- page 1. A page-3 pointer makes RDSLT switch
; page 3 to the cartridge, which serves nothing there, so the loop reads
; whatever the undriven bus holds, never sees its NUL, and prints for ever.
;
; HL = 0 takes the branch that skips the menu entirely, and the caller then
; passes no meaningful choice at all -- see TrampDskfmt.
;
; GETDPB keeps its own body now that DSKFMT has real work. It stays "other
; error" and is unreachable anyway: the jump table entry at 0x4016 is
; deliberately left pointing at the vendor's own routine, and this slot exists
; only to keep the table's indexing aligned.
; -----------------------------------------------------------------------------
TrampChoice:
    ld      hl, 0
    ret

TrampGetdpb:
    scf
    ld      a, 12
    ret

; -----------------------------------------------------------------------------
; DSKFMT
;   in   A = format choice, D = drive number, HL = work area, BC = its length
;   out  carry set on error with the code in A
;
; The drive is in D. This read it from B, which is DSKIO's register, not this
; one's -- the vendor's caller pops the drive into D and the work area into
; HL/BC (0x6141: pop af / pop de / pop iy / pop bc / pop hl, then the slot call
; to 0x401C).
;
; The choice in A is ignored. There is only one format -- the image's size
; decides everything -- so CHOICE answers "no menu", and on that path the vendor
; code reaches here with whatever the stack held in A: the interface's slot
; byte, not a choice. Testing it for 1 refused every format there was.
;
; The firmware writes the boot sector, both FATs and an empty root directory
; from the image's own size; the data area is left alone, as a real format
; leaves it. That command programs and erases flash, so it answers with /WAIT
; released -- hence MailCmd rather than a poll of our own.
; -----------------------------------------------------------------------------
TrampDskfmt:
    ld      a, d
    cp      2
    jr      nc, .refuse             ; drives A and B, like DSKIO

    di
    ld      (P_DRIVE), a
    ld      a, CMD_DISK_FORMAT
    call    MailCmd
    ei

    or      a
    ret     z                       ; "or a" cleared carry: success

    cp      ST_WRPROT
    jr      z, .protected
.refuse:
    ld      a, 12                   ; other error
    scf
    ret

.protected:
    xor     a                       ; 0 = write protected
    scf
    ret

TrampMtoff:
    ret


; -----------------------------------------------------------------------------
; BounceCopy moves one chunk between the exchange buffer and a page-1 address,
; in either direction.
;
; The window dispatcher cannot do this itself. Our exchange buffer is in page 1
; and so is the caller's buffer, only one slot can own the page at a time, and
; the dispatcher is served through that same page -- switching it away would
; take the running code with it. So the loop runs from here, in page 3, and the
; bytes travel on the stack, which is in page 3 as well and therefore
; addressable in both slot configurations. That is also why there is no bounce
; buffer in the reservation any more: the stack costs nothing on the disks that
; never take this path.
;
;   in   HL = the exchange-buffer side, DE = the MSX side,
;        BC = byte count, a nonzero multiple of HOP_SIZE,
;        carry reset = buffer -> MSX (a read), set = MSX -> buffer (a write)
;   out  DE = the MSX-side address advanced by the byte count;
;        AF, BC, HL clobbered, page 1 left with the cartridge
;
; One loop serves both directions because they are the same three steps in the
; same order -- stage a hop under one slot, switch, unstage it under the other
; -- differing only in which slot goes with which step and which pointer the
; bytes come from. A read stages out of the buffer with our own slot selected;
; a write stages out of MSX RAM with the RAM slot selected. Each hop therefore
; starts with an ENASLT that a read does not strictly need, since the cartridge
; already owns the page on entry; that is one redundant slot switch per 32 bytes
; on a path no disk tested so far has taken at all, against a second copy of
; this loop in the reservation that every disk pays for.
;
; Every hop is exactly HOP_SIZE: a chunk is a whole number of 512-byte sectors
; and HOP_SIZE divides 512, so there is never a short final hop.
; -----------------------------------------------------------------------------
BounceCopy:
    ld      (BounceLeft), bc

    ; Sort the two slot ids and the two pointers by direction. None of these
    ; loads touch the flags, so the caller's carry survives to the branch.
    ld      a, (RAMAD1)
    ld      c, a                    ; C = the slot that owns page-1 RAM
    ld      a, (TrampTable + SLOT_BYTE_OFS)
    ld      b, a                    ; B = our own slot, left there by the tail
    jr      c, .toBuffer

    ; Read: out of the buffer, into MSX RAM.
    ld      (BounceFrom), hl
    ld      (BounceTo), de
    ld      a, b
    ld      (SlotStage), a
    ld      a, c
    ld      (SlotMove), a
    xor     a                       ; the MSX side is BounceTo
    jr      .start

.toBuffer:
    ; Write: out of MSX RAM, into the buffer.
    ld      (BounceFrom), de
    ld      (BounceTo), hl
    ld      a, c
    ld      (SlotStage), a
    ld      a, b
    ld      (SlotMove), a
    ld      a, 1                    ; the MSX side is BounceFrom
.start:
    ld      (BounceDir), a

.hop:
    ; Select the staging side and read one hop out of it. The words go out last
    ; first, so that popping them back restores their order.
    ld      a, (SlotStage)
    ld      h, $40
    call    ENASLT
    ld      hl, (BounceFrom)
    ld      bc, HOP_SIZE
    add     hl, bc
    ld      (BounceFrom), hl        ; where the next hop starts
    ld      b, HOP_SIZE / 2
.stage:
    dec     hl
    ld      d, (hl)
    dec     hl
    ld      e, (hl)
    push    de
    djnz    .stage

    ; Hand page 1 to the other side. ENASLT destroys every register and uses
    ; the stack below SP, so the staged words above SP survive it.
    ld      a, (SlotMove)
    ld      h, $40
    call    ENASLT

    ld      de, (BounceTo)
    ld      b, HOP_SIZE / 2
.unstage:
    pop     hl
    ld      a, l
    ld      (de), a
    inc     de
    ld      a, h
    ld      (de), a
    inc     de
    djnz    .unstage
    ld      (BounceTo), de

    ld      hl, (BounceLeft)
    ld      bc, -HOP_SIZE
    add     hl, bc
    ld      (BounceLeft), hl
    ld      a, h
    or      l
    jr      nz, .hop

    ; Leave page 1 with the cartridge whichever way the bytes went: the window
    ; has to be readable again before this returns into it.
    ld      a, (TrampTable + SLOT_BYTE_OFS)
    ld      h, $40
    call    ENASLT

    ; Hand back the MSX-side pointer, which is the one the caller advances.
    ld      a, (BounceDir)
    or      a
    ld      de, (BounceTo)
    ret     z
    ld      de, (BounceFrom)
    ret

BounceFrom:     dw 0        ; staging side, carried across ENASLT
BounceTo:       dw 0        ; the other side, likewise
BounceLeft:     dw 0        ; bytes of the chunk still to move
SlotStage:      db 0        ; slot selected while a hop is staged
SlotMove:       db 0        ; slot selected while it is written out
BounceDir:      db 0        ; nonzero when the MSX side is BounceFrom

    ; This blob ends with a two-byte guard and the OrigStatementAddr cell,
    ; exactly as the ROM part ends with OrigInitAddr, and for the same reason:
    ; CmdDiskSetRam patches the cell by addressing the blob's last two bytes,
    ; so anything appended after it would silently move that patch onto
    ; something else -- and the scratch just above is zeroes of its own, which
    ; is what makes the guard worth having rather than a pattern check. It
    ; holds the STATEMENT address the vendor's own header published, and the
    ; blob at BAS_BASE reads it from here to chain a name that is not ours.
    ; Zero means the dump published no extended statements at all.
    ;
    ; Neither is a relocation site: the value comes from the firmware, not from
    ; this blob's assembly origin. build_stub.py checks the shape on every
    ; build.
    ASSERT $ <= TrampTable + RAM_STUB_LEN - 4
    ds (TrampTable + RAM_STUB_LEN - 4 - $), 0 ; keep the published RAM layout
    db      $5A, $A5
OrigStatementAddr:
    dw      0

    ASSERT OrigStatementAddr - TrampTable == BAS_ORIG_OFS
    ENDIF

    IF STUB_PART = 2
; =============================================================================
; WINDOW PART -- the dispatcher
;
; Served by the firmware at WIN_BASE, out of the exchange buffer, while the
; trampoline holds the window open. It is ordinary cartridge memory as far as
; the Z80 is concerned: reads come back from the buffer, and writes into the
; window range are captured into it, so the scratch at the bottom of this file
; is genuine read/write memory that costs no MSX RAM at all.
;
; Two things make running the dispatcher out of the cartridge safe, where the
; original design could not:
;
;   The firmware holds /WAIT asserted through every command handler, so the Z80
;   is stalled inside the command write and fetches nothing while a handler
;   runs. It used to drop /WAIT and stop driving the bus, which is what forced
;   every poll into MSX RAM.
;
;   The window is open only while this code is the only thing running. The
;   vendor bytes underneath WIN_BASE are live Disk BASIC in both dumps -- an
;   opcode-anchored scan finds 14 references into 0x7C00-0x7DFF in the National
;   image and 24 in the Yamaha one -- so hiding them permanently would break
;   Disk BASIC. The trampoline opens the window on the way in and closes it on
;   the way out, under DI, and nothing else executes in between.
;
; The two entries below are at fixed offsets the trampoline calls (WIN_DSKIO,
; WIN_DSKCHG). Both return with a plain ret, back into the trampoline.
; =============================================================================
WinTable:
    jp      WinDskio
    jp      WinDskchg

; Sectors per CMD_DISK_READ, up to MAX_SECTORS -- the exchange buffer's own
; capacity, so this is as large as it can be. One command per chunk, so a
; transfer of N sectors costs ceil(N / CHUNK_SECTORS) mailbox round trips
; instead of N: at 1 a full 720-sector image took over 700 of them.
;
; What bounds it from above. The firmware serves the command with /WAIT held for
; the whole flash read, and a stalled Z80 issues no M1 cycles, so it refreshes
; no MSX DRAM for the duration. At 12 sectors that read is 6144 bytes out of
; XIP, roughly 340 us, against the 2 ms in which MSX DRAM wants every row
; refreshed -- a sixth of the budget, and the Z80 catches up in about 50 us of
; ordinary execution afterwards, one row per M1. The CopyOut that follows each
; chunk is 6144 bytes of LDIR, which is nothing but M1 fetches.
;
; This was 1 for a long while, as insurance against a hang that turned out to be
; a fault in one particular machine -- the same disk fails there from a real
; floppy drive. Refresh starvation was never demonstrated. If a hang ever does
; track this figure, that is real evidence and this is the knob; short of that,
; leave it at the maximum.
;
; A write reuses the same figure for a different reason: that command is served
; with /WAIT released, so nothing about it is bounded by refresh, and the cap is
; simply how much sector data the exchange buffer holds.
CHUNK_SECTORS   EQU MAX_SECTORS

; -----------------------------------------------------------------------------
; DSKIO
;   in   A  drive, B sector count, C media descriptor, DE logical sector,
;        HL transfer address, carry reset = read / set = write
;   out  carry set on error with the code in A, B = sectors transferred
;
; Interrupts are already off and the window is already open: the trampoline did
; both, and undoes both when this returns.
; -----------------------------------------------------------------------------
WinDskio:
    ; Stash the two helpers the trampoline owns. They are relocated with it, so
    ; their addresses cannot be assembled into this blob, and they are kept in
    ; window scratch rather than in IX and IY because every bounce hop calls
    ; ENASLT, which is documented to destroy both.
    ld      (WBounce), ix
    ld      (WMail), iy

    ; Direction first: the drive check below needs A and clears carry, and both
    ; paths share everything after it.
    ld      (SavedDrive), a
    sbc     a, a                    ; 0xFF when the caller set carry: a write
    ld      (SavedDir), a

    ; Two drives: A is loader.ini's last=, B is its disk2=. The drive number
    ; goes to the firmware in P_DRIVE and it picks the image; a drive with no
    ; image answers ST_NOTREADY, which .ioError turns back into DSKIO's code 2.
    ; Anything past drive B is refused here -- the vendor ROM's DRIVES routine
    ; is not in the patched jump table, so it may advertise more than this
    ; emulates, and serving one image under several letters is worse than
    ; refusing.
    ld      a, (SavedDrive)
    cp      2
    jr      c, .driveOk
    ld      b, 0
    ld      a, 2                    ; not ready
    scf
    ret

.driveOk:
    ; Save the parameters one register at a time. "ld (SavedCount), bc" would
    ; store C at SavedCount and B at the byte after it, which is backwards.
    ; The media descriptor in C is not saved at all: the firmware addresses the
    ; image by LBA, so nothing here ever reads it back.
    ld      (SavedAddr), hl
    ld      (SavedLba), de
    ld      a, b
    ld      (SavedCount), a         ; sectors wanted
    xor     a
    ld      (DoneCount), a

.nextChunk:
    ld      a, (SavedCount)
    or      a
    jr      z, .done                ; nothing left to do

    ; Clamp this chunk. On a read CHUNK_SECTORS bounds how long the firmware
    ; holds /WAIT, so the Z80 never starves MSX DRAM refresh; on a write the
    ; line is released anyway and the same cap is just what the buffer holds.
    ; See the EQU.
    cp      CHUNK_SECTORS + 1
    jr      c, .chunkOk
    ld      a, CHUNK_SECTORS
.chunkOk:
    ld      (ChunkCount), a

    ; Fill the parameter block.
    ld      hl, (SavedLba)
    ld      a, l
    ld      (P_LBA_LO), a
    ld      a, h
    ld      (P_LBA_MID), a
    xor     a
    ld      (P_LBA_HI), a           ; 24-bit LBA; DSKIO only ever gives us 16
    ld      a, (ChunkCount)
    ld      (P_COUNT), a
    ld      a, (SavedDrive)
    ld      (P_DRIVE), a

    ld      a, (SavedDir)
    or      a
    jr      nz, .writeChunk

    ld      a, CMD_DISK_READ
    call    ExecCmd
    or      a
    jr      nz, .ioError            ; A still holds the firmware's status

    ; Copy the sectors out of the exchange buffer.
    ld      a, (ChunkCount)
    call    CopyOut
    jr      .advance

.writeChunk:
    ; The caller's bytes go into the exchange buffer first. That needs no
    ; command: buffer-range writes are captured whatever the overlay is doing,
    ; and the overlay only ever decides what a *read* there answers with.
    ld      a, (ChunkCount)
    call    CopyIn

    ; Through the trampoline, not ExecCmd: this is the command the firmware
    ; answers with /WAIT released, so the poll has to run from MSX RAM. See
    ; MailCmd.
    ld      a, CMD_DISK_WRITE
    call    CallMail
    or      a
    jr      nz, .writeError

.advance:
    ; Advance LBA, transfer address and the remaining count.
    ld      a, (ChunkCount)
    ld      c, a
    ld      b, 0
    ld      hl, (SavedLba)
    add     hl, bc
    ld      (SavedLba), hl

    ; DoneCount is a single byte; load it as one and clear H ourselves rather
    ; than "ld hl,(DoneCount)", which would read the byte that follows it in
    ; memory into H before the next line discards it.
    ld      a, (DoneCount)
    ld      l, a
    ld      h, 0
    add     hl, bc
    ld      a, l
    ld      (DoneCount), a

    ld      a, (SavedCount)
    sub     c
    ld      (SavedCount), a
    jr      .nextChunk

.done:
    ld      a, (SavedDir)
    or      a
    jr      z, .closeOk

    ; A write leaves the data programmed but the flash filesystem's own sector
    ; map living only in the Pico's RAM, and the page each sector used to
    ; occupy already freed. Persist it before telling the caller the write
    ; succeeded. Once per DSKIO call rather than once per chunk: the map lands
    ; in one 4 KB flash sector, so syncing per chunk would erase that same
    ; sector once for every 512 bytes written.
    ld      a, CMD_DISK_FLUSH
    call    CallMail
    or      a
    jr      nz, .writeError         ; data programmed, map not persisted -- the
                                    ; write is not safe yet, so say so
.closeOk:
    ld      a, (DoneCount)
    ld      b, a                    ; save the transferred count before the
                                     ; close call below clobbers A
    ; Close the exchange-buffer overlay before returning. "or a" after it
    ; deliberately reinstates the success carry state regardless of what
    ; ExecCmd's own status left in A and the flags.
    ld      a, CMD_CLOSE_BUFFER
    call    ExecCmd
    or      a                       ; clear carry: success
    ret

.writeError:
    ; Two DSKIO codes come out of a failed write, and the caller acts on them
    ; differently: 0 means the disk refused and retrying is pointless, 10 means
    ; the write itself failed. Carried in C, which DSKIO does not return, so it
    ; survives the buffer-close round trip below.
    cp      ST_WRPROT
    ld      c, 0                    ; write protected
    jr      z, .failWith
    cp      ST_NOTREADY
    ld      c, 2                    ; not ready: nothing in that drive
    jr      z, .failWith
    ld      c, 10                   ; write fault
.failWith:
    ld      a, (DoneCount)
    ld      b, a
    ld      a, CMD_CLOSE_BUFFER
    call    ExecCmd
    ld      a, c
    scf
    ret

.ioError:
    ; Two DSKIO codes come out of a failed read as well: an empty drive is "not
    ; ready" and everything else is a data error. Carried in C, which DSKIO does
    ; not return, so it survives the buffer-close round trip below.
    cp      ST_NOTREADY
    ld      c, 2                    ; not ready
    jr      z, .readFailWith
    ld      c, 4                    ; data (CRC) error -- the generic failure

.readFailWith:
    ld      a, (DoneCount)
    ld      b, a                    ; save the transferred count before the
                                     ; close call below clobbers A

    ; Close the overlay before returning -- see .done. In practice
    ; CmdDiskRead's failure path never opens it (Execute() clears return_buffer
    ; at the top of every command dispatch, including this failed one), so this
    ; is defensive rather than load-bearing today; it costs one mailbox round
    ; trip and keeps the two exits symmetric.
    ld      a, CMD_CLOSE_BUFFER
    call    ExecCmd
    ld      a, c
    scf
    ret

; -----------------------------------------------------------------------------
; ExecCmd issues one mailbox command and waits for it to complete.
; -----------------------------------------------------------------------------
ExecCmd:
    ; One mailbox command, start to finish.
    ;   in   A = command code
    ;   out  A = status, 0 on success
    ;
    ; Two addresses, and neither read changes anything on the cartridge side.
    ;
    ; The firmware holds /WAIT through the whole handler, so by the time this
    ; write completes the result is already standing and the poll below is a
    ; formality. It is kept because it costs two reads and it is what makes a
    ; floating bus impossible to mistake for an answer: an undriven MSX bus
    ; reads the same held value at every address, so requiring CART_CMD to read
    ; the token AND CART_STATUS to read something else can only be satisfied by
    ; a cartridge that is genuinely driving. No status is 0xA5 (00/02/04/FE).
    ld      (CART_CMD), a
.wait:
    ld      a, (CART_CMD)
    cp      CMD_READY_TOKEN         ; token at 0x7FFF?
    jr      nz, .wait
    ld      a, (CART_STATUS)        ; status at 0x7FFE
    cp      CMD_READY_TOKEN         ; same value here means the bus is floating,
    jr      z, .wait                ; not answering -- no real status is 0xA5
    ret

; -----------------------------------------------------------------------------
; CopyOut moves A sectors from the exchange buffer to (SavedAddr), advancing
; SavedAddr as it goes.
;
; A chunk copies directly only if the whole transfer stays clear of page 1; our
; exchange buffer lives there too, and only one slot can own the page at a time.
; Any chunk that touches page 1 anywhere in its range -- even if it starts or
; ends outside it -- goes through BounceCopy in the trampoline, because this
; code cannot switch the page it is being served through. A chunk whose
; destination has already walked past page 1 is unaffected by that switch:
; ENASLT only ever touches page 1.
; -----------------------------------------------------------------------------
CopyOut:
    ld      (CopyRemain), a

    ; A chunk needs the bounce path unless the whole transfer stays clear of
    ; page 1: either it starts at or above page 2 (0x8000), or its last byte
    ; still falls below page 1 (0x4000). Checking only SavedAddr's own page at
    ; chunk entry is not enough -- a chunk of up to MAX_SECTORS sectors can walk
    ; across a page boundary before the transfer ends. Clamping the chunk to the
    ; boundary instead is not an option: an unaligned SavedAddr can leave fewer
    ; than 512 bytes before it, which would clamp to zero sectors and stall the
    ; loop.
    ld      de, (SavedAddr)
    ld      a, d
    and     $80
    jr      nz, .direct              ; SavedAddr >= 0x8000: stays clear of page 1

    ld      a, (CopyRemain)
    call    SectorsToBytes           ; BC = CopyRemain * 512; DE untouched
    ex      de, hl                   ; HL = SavedAddr
    add     hl, bc
    dec     hl                       ; HL = address of the chunk's last byte
    ld      a, h
    and     $C0
    jr      nz, .viaBounce           ; last byte >= 0x4000: chunk touches page 1
    ; else: last byte < 0x4000, chunk stays entirely in page 0 -- fall through

.direct:
    ; Direct path: one LDIR for the whole chunk.
    ld      a, (CopyRemain)
    call    SectorsToBytes
    ld      hl, CART_BUF
    ld      de, (SavedAddr)
    ldir
    ld      (SavedAddr), de
    ret

.viaBounce:
    ; Page-1 destination: hand the whole chunk to the trampoline, which is in
    ; MSX RAM and survives the slot switch this code could not. Its address
    ; came in at entry, because it is relocated to wherever the INIT tail
    ; reserved RAM and so cannot be assembled into this blob.
    ;
    ; The zero test is not dead code: BounceCopy loops before it tests, so a
    ; zero byte count would run it 2048 times and walk 64 KB of the machine.
    ld      a, (CopyRemain)
    or      a
    ret     z
    call    SectorsToBytes           ; BC = byte count
    ld      hl, CART_BUF
    ld      de, (SavedAddr)
    or      a                        ; carry reset: buffer -> MSX
    call    CallBounce
    ld      (SavedAddr), de          ; BounceCopy returns the advanced address
    ret

; -----------------------------------------------------------------------------
; CopyIn is CopyOut's mirror: it moves A sectors from (SavedAddr) into the
; exchange buffer, advancing SavedAddr as it goes. The page-1 test is the same
; test for the same reason -- the caller's buffer and ours cannot both own the
; page -- so a chunk that touches page 1 goes through the trampoline as well,
; with the direction flag set.
; -----------------------------------------------------------------------------
CopyIn:
    ld      (CopyRemain), a

    ld      de, (SavedAddr)
    ld      a, d
    and     $80
    jr      nz, .direct              ; SavedAddr >= 0x8000: clear of page 1

    ld      a, (CopyRemain)
    call    SectorsToBytes           ; BC = CopyRemain * 512; DE untouched
    ex      de, hl                   ; HL = SavedAddr
    add     hl, bc
    dec     hl                       ; HL = address of the chunk's last byte
    ld      a, h
    and     $C0
    jr      nz, .viaBounce           ; last byte >= 0x4000: touches page 1

.direct:
    ld      a, (CopyRemain)
    call    SectorsToBytes
    ld      hl, (SavedAddr)
    ld      de, CART_BUF
    ldir
    ld      (SavedAddr), hl          ; here it is the source that advanced
    ret

.viaBounce:
    ld      a, (CopyRemain)
    or      a
    ret     z
    call    SectorsToBytes           ; BC = byte count
    ld      hl, CART_BUF
    ld      de, (SavedAddr)
    scf                              ; carry set: MSX -> buffer
    call    CallBounce
    ld      (SavedAddr), de
    ret

; Both helpers live in the trampoline, wherever the INIT tail put it, so they
; are reached through the addresses it handed over at entry. Reloading IX here
; rather than trusting it to survive is the point: every bounce hop calls
; ENASLT, which destroys it.
CallBounce:
    ld      ix, (WBounce)
    jp      (ix)

CallMail:
    ld      ix, (WMail)
    jp      (ix)

; SectorsToBytes turns a sector count in A into a byte count in BC. A is at
; most MAX_SECTORS, so A * 256 cannot overflow H and one doubling finishes the
; job -- which is why this is not a chain of nine "add hl,hl".
SectorsToBytes:
    ld      h, a
    ld      l, 0                    ; HL = A * 256
    add     hl, hl                  ; HL = A * 512
    ld      b, h
    ld      c, l
    ret

; -----------------------------------------------------------------------------
; DSKCHG
;   in   A drive, B 0, C media descriptor, HL DPB pointer
;   out  carry set on error with the code in A
;        B = 1 unchanged, 0 unknown, -1 changed
;
; The firmware raises its changed flag when CDiskImage::Mount succeeds (via
; CMD_MAP_DISK) and clears it when queried, so a fresh mount is reported
; exactly once. On a change the caller issues GETDPB itself -- do not call it
; from here, it needs B and C, which this entry does not preserve.
; -----------------------------------------------------------------------------
WinDskchg:
    push    hl
    ld      (P_DRIVE), a
    ld      a, CMD_DISK_STATUS
    call    ExecCmd
    or      a
    jr      nz, .err

    ld      a, (P_FLAGS)
    and     $01
    pop     hl
    ld      c, a                    ; save the changed-bit test result across
                                     ; the buffer-close round trip below

    ld      a, CMD_CLOSE_BUFFER
    call    ExecCmd

    ld      a, c
    or      a
    jr      z, .unchanged

    ld      b, $FF                  ; changed
    xor     a
    ret

.unchanged:
    ld      b, 1
    xor     a
    ret

.err:
    pop     hl
    ld      a, 2                    ; not ready
    scf
    ret

; --- dispatcher scratch ------------------------------------------------------
; Inside the window, so these are bytes of the firmware's exchange buffer rather
; than of MSX RAM. A read command fills at most MAX_SECTORS sectors, which stops
; below WIN_BASE, so sector data can never reach them.
SavedCount:     db 0
SavedAddr:      dw 0
SavedLba:       dw 0
SavedDrive:     db 0
SavedDir:       db 0        ; nonzero for a write
ChunkCount:     db 0
DoneCount:      db 0
CopyRemain:     db 0
WBounce:        dw 0        ; trampoline's BounceCopy, handed over at entry
WMail:          dw 0        ; trampoline's MailCmd, likewise

WinEnd:
    ; The firmware serves WIN_SIZE bytes from WIN_BASE, so anything past this
    ; point is outside the window and would read back as vendor ROM.
    ASSERT WinEnd - WinTable <= WIN_SIZE
    ENDIF

    IF STUB_PART = 3
; =============================================================================
; BASIC PART -- the Disk BASIC statement handler
;
; Served at BAS_BASE out of the exchange buffer. BASIC's CALSLT enters at
; BAS_ENTRY, which opens the entire window. MSX BASIC finds it through the STATEMENT
; vector in the ROM header at 0x4004, which CRomPatcher::PatchStatement
; redirected to BAS_ENTRY; the address that vector used to hold is kept in the
; trampoline's OrigStatementAddr cell, where CmdDiskSetRam puts it.
;
; Most of what is here is the door: a name that is not in the table below goes
; to the vendor's own handler untouched. CALL FORMAT and CALL SYSTEM are the two
; that matter -- both are the vendor's -- and if they still work, the door is
; hung correctly.
;
; The table's own names are CALL OMOUNT and CALL OEJECT, which change the disk in
; a running machine, CALL ODISK, which says what is in each drive, and CALL ODIR,
; which lists the cartridge's own volume. Everything the first two do is: parse
; the literal, put the path in the exchange buffer and the drive in the
; parameter block, send one command, and print a line if it failed. The
; firmware's Mount() raises the disk-changed flag, so the next DSKCHG tells Disk
; BASIC the media changed and the DPB is re-read from the new image's own boot
; sector -- nothing here has to invalidate anything. CALL ODISK parses nothing
; and sends one command of its own, then prints what came back. CALL ODIR is the
; only one that is a loop: one command per directory entry, for as long as the
; directory lasts.
;
; Three things make this part different from the dispatcher in the other
; window, and all three are consequences of who calls it:
;
;   Nothing of ours runs first. The trampoline opens the dispatcher's window
;   and calls into it, so it can hand over addresses in IX and IY. Here BASIC
;   arrives straight from the header vector, with the window opened by the bus
;   loop on the entry fetch itself. So everything this blob needs to know about
;   MSX RAM it has to find for itself -- see TRAMP_BASE_PTR.
;
;   It cannot close its own window. The instruction fetched after the closing
;   write already comes from the vendor's bytes, so both exits live in the
;   trampoline: BasicRet and BasicChain. MailCmd is there for a related reason
;   -- while the firmware answers a slow command it serves no bus at all, so a
;   poll fetched from this window would be reading its own opcodes off an
;   undriven one.
;
;   It is fixed at BAS_BASE, like the window part at WIN_BASE. Nothing
;   relocates it, so STUB_ORG is ignored and build_stub.py checks that the
;   bytes come out identical at any origin.
; =============================================================================
; Helpers occupy the sector below the original entry. Keep 7800h stable for
; existing DOS clients and ROM statement dispatch; no extra MSX RAM is needed.
BasOdel:
    ld      a, CMD_FILE_DELETE
    jr      BasFile
BasOcopy:
    ld      a, CMD_FILE_COPY
    jr      BasFile
BasOnew:
    ld      a, CMD_FILE_NEW
BasFile:
    ld      (BasCmd), a
    ld      hl, (BasText)
    call    BasSpace
    cp      '('
    jp      nz, BasChain
    inc     hl
    ld      de, CART_BUF
    call    BasFilePath
    jp      c, BasChain
    ld      a, (BasCmd)
    cp      CMD_FILE_COPY
    jr      nz, .size
    call    BasSpace
    cp      ','
    jp      nz, BasChain
    inc     hl
    ld      de, CART_BUF + 65
    call    BasFilePath
    jp      c, BasChain
    jr      .end
.size:
    cp      CMD_FILE_NEW
    jr      nz, .end
    ld      de, 720
    call    BasSpace
    cp      ','
    jr      nz, .storeSize
    inc     hl
    call    BasSpace
    cp      $1C                    ; BASIC's tokenized integer literal
    jr      nz, .decimal
    inc     hl
    ld      e, (hl)
    inc     hl
    ld      d, (hl)
    inc     hl
    push    hl
    ld      hl, -360
    add     hl, de
    ld      a, h
    or      l
    pop     hl
    jr      z, .storeSize
    ld      a, d
    cp      2
    jp      nz, BasChain
    ld      a, e
    cp      $D0
    jp      nz, BasChain
    jr      .storeSize
.decimal:
    ld      de, 360
    cp      '3'
    jr      z, .three
    cp      '7'
    jp      nz, BasChain
    ld      de, 720
    inc     hl
    ld      a, (hl)
    cp      '2'
    jr      .lastDigit
.three:
    inc     hl
    ld      a, (hl)
    cp      '6'
.lastDigit:
    jp      nz, BasChain
    inc     hl
    ld      a, (hl)
    cp      '0'
    jp      nz, BasChain
    inc     hl
.storeSize:
    ld      (CART_BUF + 130), de
.end:
    call    BasSpace
    cp      ')'
    jp      nz, BasChain
    inc     hl
    call    BasSpace
    or      a
    jr      z, .send
    cp      ':'
    jp      nz, BasChain
.send:
    ld      (BasText), hl
    ld      a, (BasCmd)
.loop:
    call    BasMail                 ; poll from MSX RAM while flash is busy
    cp      ST_FILE_MORE                    ; more sectors remain
    jr      nz, .done
    ld      a, CMD_FILE_STEP
    jr      .loop
.done:
    or      a
    jp      z, BasDone
    jp      BasFail

; Mandatory nonempty quoted path, up to 64 bytes. No drive prefix: these
; operations name files on the outer volume, not either emulated drive.
BasFilePath:
    call    BasSpace
    cp      '"'
    jr      nz, .bad
    inc     hl
    ld      b, 64
.byte:
    ld      a, (hl)
    inc     hl
    cp      '"'
    jr      z, .end
    cp      32
    jr      c, .bad
    cp      ':'
    jr      z, .bad
    inc     b
    dec     b
    jr      z, .bad
    ld      (de), a
    inc     de
    dec     b
    jr      .byte
.end:
    ld      a, b
    cp      64
    jr      z, .bad
    xor     a
    ld      (de), a
    ret
.bad:
    scf
    ret

    ASSERT $ <= BAS_ENTRY
    ds BAS_ENTRY - $, 0
BasEntry:
    ; The header vector points at this address, so this instruction is the
    ; blob's first byte and must stay first.
    ;
    ; Interrupts off, and then the two interrupt hooks neutralised, because a
    ; di alone does not hold. The BIOS entries this blob prints and pauses
    ; through -- CHPUT, SNSMAT -- end their own di/ei pairs with an
    ; unconditional ei, so a VDP tick pending when one of them runs is taken
    ; inside the call, with the window open. The handler that tick runs stays
    ; in page 0 and RAM until it reaches H.TIMI, and on both supported dumps
    ; H.TIMI is the Disk ROM's motor-off timer, at 0x7837 (National) or 0x7827
    ; (Yamaha): inside this window, where the bytes the hook CALLFs into are
    ; this blob's rather than the timer's. That would hang CALL ODIR every
    ; time and CALL ODISK often -- read off the dumps' own bytes, not measured
    ; on a machine: the mechanism is certain, the symptom is inferred. So a
    ; ret is written over the first byte of H.KEYI and of H.TIMI below, and
    ; the bytes go back in BasHooksBack, which both exits run before the
    ; trampoline is entered: with the hooks a ret, the BIOS handler never
    ; enters page 1 at all, whatever the BIOS does to the interrupt flag.
    ; Nothing is lost by it -- the timer is spinning down a motor this
    ; cartridge does not have, and H.KEYI is a ret on a stock machine -- and a
    ; statement that crashed half-way would leave both neutralised until the
    ; next reset, which costs the same nothing.
    ;
    ; The di itself has nothing to do on the path BASIC takes. CALSLT's slot
    ; switch disables interrupts and its final CALL CLPRM1 / JP (IX) enables
    ; nothing on the way to this byte -- that is the Red Book's listing of
    ; CLPRIM, and its note that every slot-switching routine leaves interrupts
    ; off -- so the window opens on the fetch of this byte with interrupts
    ; already disabled, and the hook writes below are atomic against a tick.
    ; It stays because it is one byte and is what the rest of this blob is
    ; written against: a caller that arrived with them on (none today) would
    ; be given the same guarantee by it.
    ;
    ; The ei on the way out is in BasicClose. BASIC runs with interrupts
    ; enabled and CALSLT turned them off on the way in, so it is the way to be
    ; sure they are on when BASIC gets the machine back; if some caller ever
    ; arrives with them already off, that ei hands it a machine whose keyboard
    ; and clock are running again, which is the harmless direction to be wrong
    ; in.
    di

    ; Save the first byte of each hook and make both a ret. Only the first
    ; byte: a hook is a jp, a CALLF or a ret, and the opcode is what decides
    ; which, so one byte is the whole difference between a hook that runs and
    ; one that returns. Saved into window scratch, which costs no MSX RAM.
    ld      a, (H_KEYI)
    ld      (BasKeyiSave), a
    ld      a, (H_TIMI)
    ld      (BasTimiSave), a
    ld      a, $C9                  ; ret
    ld      (H_KEYI), a
    ld      (H_TIMI), a

    ; BASIC's text pointer. The chain path has to hand it to the vendor exactly
    ; as it arrived, and a statement of ours hands back where it stopped
    ; parsing, so it is parked in window scratch rather than kept in a register
    ; the work in between would want.
    ld      (BasText), hl

    ; Resolve the three trampoline addresses into the operands that use them,
    ; from the trampoline's base and the offsets the RAM part ASSERTs it puts
    ; them at: the two exits, and MailCmd for the slow commands.
    ;
    ; Window scratch rather than IX and IY, which is where the dispatcher
    ; started and why it stopped: the statements here call into the BIOS --
    ; CHPUT to print, SNSMAT to pause a listing later -- and a BIOS entry is
    ; free to destroy both index registers, as ENASLT is documented to. Writes
    ; into this window are captured into the exchange buffer whatever the
    ; overlay is doing, so the operands are genuine read/write memory that cost
    ; no MSX RAM at all.
    ;
    ; Walked as three deltas from one base rather than three sums, because the
    ; offsets are adjacent by construction -- see BAS_MAIL_OFS.
    ld      hl, (TRAMP_BASE_PTR)
    ld      de, BAS_RET_OFS
    add     hl, de
    ld      (BasRetVec), hl
    ld      de, BAS_CHAIN_OFS - BAS_RET_OFS
    add     hl, de
    ld      (BasChainVec), hl
    ld      de, BAS_MAIL_OFS - BAS_CHAIN_OFS
    add     hl, de
    ld      (BasMailVec), hl

; -----------------------------------------------------------------------------
; Is this name one of ours?
;
; BASIC leaves it in PROCNM, uppercased and NUL-terminated, and the table below
; holds ours as counted strings. A name that runs out at the same byte the
; table's does is a match; anything else chains.
;
; Both halves of that matter. Comparing only the table's bytes would make every
; vendor name that begins with one of ours -- OMOUNTED against OMOUNT -- our own,
; and it would be taken silently: the argument would parse as nonsense and the
; disk would change under a program that asked for something else entirely.
; -----------------------------------------------------------------------------
    ld      hl, BasNames
.name:
    ld      a, (hl)                 ; the counted length, 0 ends the table
    or      a
    jp      z, BasChain
    ld      b, a
    inc     hl
    ld      de, PROCNM
.char:
    ld      a, (de)
    cp      (hl)
    jr      nz, .next
    inc     hl
    inc     de
    djnz    .char
    ld      a, (de)                 ; the table's name ended -- PROCNM must too
    or      a
    jr      nz, .next
    ld      a, (hl)                 ; the handler follows the name
    inc     hl
    ld      h, (hl)
    ld      l, a
    jp      (hl)
.next:
    ; B name bytes are still unread -- zero when the name matched and PROCNM
    ; went on -- and then the two bytes of the handler address.
    ld      e, b
    ld      d, 0
    add     hl, de
    inc     hl
    inc     hl
    jr      .name

; -----------------------------------------------------------------------------
; CALL OMOUNT("[X:]path") and CALL OEJECT(["X:"]).
;
; One body: both are a parsed argument and one command, and the command code is
; the only difference between them. EJECT ignores the path the firmware is
; handed, which costs nothing and keeps CALL OEJECT("B:") -- prefix, no path --
; from needing a case of its own.
; -----------------------------------------------------------------------------
BasMount:
    ld      a, CMD_BASIC_MOUNT
    jr      BasMedia
BasEject:
    ld      a, CMD_BASIC_EJECT
BasMedia:
    ld      (BasCmd), a
    ld      hl, (BasText)           ; the name match walked HL through the table
    call    BasArg
    jp      c, BasChain             ; not an argument of ours: BASIC's own error

    ; Where the parser stopped is what BASIC gets back, whatever happens from
    ; here: leave HL inside the literal and BASIC resumes execution in the
    ; middle of a file name.
    ld      (BasText), hl

    ; A refusal the parser decided on its own -- a drive letter this cartridge
    ; does not have, a path longer than the firmware accepts -- is reported
    ; without asking the firmware anything.
    ld      hl, (BasMsg)
    ld      a, h
    or      l
    jr      nz, BasSay

    ld      a, (BasCmd)
    call    BasMail
    or      a
    jr      z, BasDone              ; BASIC's own "Ok" is the confirmation

    ; ST_NOTREADY cannot reach BasFail from here: a drive that is not A or B was
    ; refused above, and drives A and B always exist on the firmware's side
    ; whether or not they hold a disk.

; -----------------------------------------------------------------------------
; BasFail -- a status turned into one of the two lines a statement can say.
;
;   in   A = a nonzero status
;
; Two outcomes and two lines. Anything that is not "no such file" is the
; cartridge failing to do the job -- MailCmd's own ST_TIMEOUT when nothing came
; back at all, or the firmware's 0xFE for a command it did not decode -- and a
; BASIC program can do nothing different about either.
;
; Shared by every statement that sends a command, so that a mount, a listing
; and a directory that does not exist all report a missing name in the same
; words. Falls straight into BasSay.
; -----------------------------------------------------------------------------
BasFail:
    ld      hl, MsgNotFound
    cp      ST_NOFILE
    jr      z, BasSay
    cp      $12
    ld      hl, MsgExists
    jr      z, BasSay
    cp      $14
    ld      hl, MsgInUse
    jr      z, BasSay
    cp      $18
    ld      hl, MsgFull
    jr      z, BasSay
    cp      $0A
    ld      hl, MsgWriteProtected
    jr      z, BasSay
    cp      $16
    ld      hl, MsgInvalid
    jr      z, BasSay
    cp      $04
    ld      hl, MsgIOError
    jr      z, BasSay
    ld      hl, MsgNoAnswer
BasSay:
    call    BasPrint
BasDone:
    ld      hl, (BasText)
    jp      BasGoRet

; -----------------------------------------------------------------------------
; CALL ODISK -- what is in each drive.
;
; No argument, so nothing is parsed and BASIC's text pointer goes back exactly
; as it arrived: whatever the user typed after the name is BASIC's own syntax
; error to report, which is the right one and costs nothing here.
;
; The firmware answers with two records at the base of the exchange buffer, one
; per drive -- see CMD_BASIC_INFO. Only the cartridge failing to answer is an
; error worth a line; there is nothing here for "no such file" to mean, because
; nothing is being opened.
; -----------------------------------------------------------------------------
BasDisk:
    ld      a, CMD_BASIC_INFO
    call    BasMail
    or      a
    ld      hl, MsgNoAnswer
    jr      nz, BasSay

    ld      hl, CART_BUF
    ld      a, 'A'
    call    BasDrvLine
    ld      a, 'B'
    call    BasDrvLine
    jr      BasDone

; -----------------------------------------------------------------------------
; BasDrvLine -- one drive's line of that report.
;
;   in   A = the drive letter, HL = the drive's record
;   out  HL = past the record, i.e. at the next drive's
;
; "A: GAME2.DSK  720K", with "  (protected)" after it when the image is
; read-only and "-- empty --" in place of the lot when the drive holds nothing.
; The path is printed straight out of the buffer, which is also what walks HL
; to the next record: BasPutz leaves it past the NUL either way, and an empty
; drive's path is just that NUL.
; -----------------------------------------------------------------------------
BasDrvLine:
    call    BasPutc
    ld      a, ':'
    call    BasPutc
    ld      a, ' '
    call    BasPutc

    ld      c, (hl)                 ; state: bit 0 mounted, bit 1 protected
    inc     hl
    ld      e, (hl)                 ; sector count, little-endian
    inc     hl
    ld      d, (hl)
    inc     hl                      ; HL -> the path
    call    BasPutz

    ; The record pointer is the answer, and everything below needs HL for
    ; strings of its own, so it goes on the stack until the line is finished.
    push    hl
    ld      hl, MsgEmpty
    bit     0, c
    jr      z, .say

    ld      a, ' '
    call    BasPutc
    ld      a, ' '
    call    BasPutc
    ex      de, hl
    push    bc                      ; BasKB counts its own digits in C
    call    BasKB
    pop     bc

    ld      hl, MsgProtected
    bit     1, c
    jr      z, .done                ; writable: the line is already finished

.say:
    call    BasPutz
.done:
    pop     hl
    jp      BasEol

; -----------------------------------------------------------------------------
; BasKB -- a sector count as kilobytes, then the "K".
;
;   in   HL = sectors
;   out  BC, DE, HL all clobbered -- C is this routine's own digit flag, which
;        is why BasDrvLine saves the state byte across the call
;
; A sector is 512 bytes, so kilobytes is one shift. What is printed is the
; number, not a fixed field: leading zeros are suppressed and the width follows
; the value.
;
; Five digits rather than the three a floppy needs, because nothing bounds what
; is mounted to a floppy. CDiskImage::Mount whitelists no size at all -- any
; file that is a whole number of 512-byte sectors mounts, and loader.ini's
; last= and disk2= can name one -- so a 1.44 MB image is 1440K and a three-digit
; field would print its leading 14 as the character '>'. Five digits is exactly
; the range of the 16-bit count the reply carries, and CmdBasicInfo clamps to
; that rather than letting a larger image wrap into a plausible small one.
; -----------------------------------------------------------------------------
BasKB:
    srl     h
    rr      l                       ; HL = kilobytes
    ld      c, 0                    ; bit 0: a digit has been printed
    ld      de, 10000
    call    .digit
    ld      de, 1000
    call    .digit
    ld      de, 100
    call    .digit
    ld      de, 10
    call    .digit
    ld      a, l                    ; the units, which always print
    add     a, '0'
    call    BasPutc
    ld      a, 'K'
    jp      BasPutc

.digit:
    ; How many DE go into HL, by subtraction: there is no divide, and the four
    ; places this is called from are the whole of the arithmetic this blob does.
    ld      b, '0' - 1
.sub:
    inc     b
    or      a
    sbc     hl, de
    jr      nc, .sub
    add     hl, de                  ; one too far: give it back
    ld      a, b
    cp      '0'
    jr      nz, .show
    bit     0, c
    ret     z                       ; a leading zero: not a digit yet
.show:
    set     0, c
    jp      BasPutc

; -----------------------------------------------------------------------------
; CALL ODIR(["path"]) -- the cartridge's own volume, one entry to a line.
;
; The one statement that is a loop: CMD_BASIC_DIR_OPEN opens a directory and
; CMD_BASIC_DIR_NEXT steps it, one round trip per entry, until the firmware
; answers ST_ENDDIR. The firmware closes its own handle on that status, and the
; window close every exit below sends closes it on any path that stops early,
; so nothing here has to carry a handle it might drop.
;
; The argument goes through BasArg, the parser CALL OMOUNT uses, so an overlong
; path is refused here in the same words and the literal reaches the exchange
; buffer bounded. That parser also takes an "X:" prefix off the front and puts
; the drive in P_DRIVE, which no directory command reads: a listing is of the
; cartridge's own volume, which has no drives in it. It costs nothing, because a
; FAT name cannot contain a colon -- a prefix can never be part of a path the
; volume could have opened anyway.
;
; The whole listing runs with the exchange-buffer overlay open, re-opened once
; per entry, while this blob is itself being served out of that buffer. Why
; that is safe is CmdBasicDirNext's comment in DiskCart.cxx: both branches of
; the firmware's read path resolve to the same byte of the same array, so an
; open overlay is unobservable to code running in the window. It stays safe
; only while nothing but this blob executes out of page 1 for the length of
; the statement: CHPUT and SNSMAT are page 0, nothing here calls anything that
; is not, and the one thing that would have entered page 1 uninvited -- the
; interrupt hook H.TIMI, which on both dumps points into this very window --
; is a ret from BasEntry to the exit. Not "under di": the BIOS's own ei inside
; every CHPUT lets a tick in, and this listing makes over a thousand of them.
;
; The listing opens with the path it is of -- "/" and then the argument -- and
; goes on with one entry to a line. The header is printed from BASIC's own text
; rather than read back out of the exchange buffer, because the path there is
; not readable: the buffer answers reads only while a reply has the overlay
; open, the open's reply is a status alone, and a read of CART_BUF without one
; answers the vendor's ROM bytes (Execute()'s read path in DiskCart.cxx). The
; write that put the path there was captured all the same, which is how the
; firmware got it. BasArg leaves BasPath at
; the literal's first byte past any drive prefix -- which no directory command
; reads, so the header leaves it out -- and the closing quote ends the walk.
; After the open and before the first step, so that a directory that cannot be
; opened is refused without a header and an empty one is its header alone.
; -----------------------------------------------------------------------------
BasDir:
    ld      hl, (BasText)           ; the name match walked HL through the table
    call    BasArg
    jp      c, BasChain             ; not an argument of ours: BASIC's own error
    ld      (BasText), hl

    ld      hl, (BasMsg)            ; a refusal the parser decided on its own
    ld      a, h
    or      l
    jp      nz, BasSay

    ld      a, CMD_BASIC_DIR_OPEN
    call    BasMail
    or      a
    jp      nz, BasFail             ; no such directory, or no answer at all

    ; The header, which is the first line of the first page: "/" and then the
    ; argument, less a "/" of its own so that the root lists as "/" and not as
    ; "//". The closing quote is the walk's only bound, and it is there to be
    ; found: BasArg walked this same literal to its closing quote already and
    ; refused the statement (its .bad path, which chains) when there was none,
    ; so a walk that starts here cannot run off the end of the line. With no
    ; argument BasPath is at an empty literal's closing quote, so the walk ends
    ; at once -- see MsgNoPath.
    ld      a, DIR_PAGE_LINES - 1
    ld      (BasPage), a
    ld      a, '/'
    call    BasPutc
    ld      hl, (BasPath)
    ld      a, (hl)
    cp      '/'
    jr      nz, .header
    inc     hl
.header:
    ld      a, (hl)
    inc     hl
    cp      '"'
    jr      z, .opened
    call    BasPutc
    jr      .header
.opened:
    call    BasEol

.entry:
    ld      a, CMD_BASIC_DIR_NEXT
    call    BasMail
    or      a
    jr      nz, .ended

    ; A full page stops to ask for a key. Before the line rather than after
    ; it: a listing whose last line exactly fills a page has nothing left to
    ; show, and stopping there would be a keypress spent on a screen nobody is
    ; waiting for.
    ld      a, (BasPage)
    or      a
    jr      nz, .count
    call    BasPause
    ld      a, DIR_PAGE_LINES
.count:
    dec     a
    ld      (BasPage), a
    call    BasName
    jr      .entry

.ended:
    cp      ST_ENDDIR
    jp      nz, BasFail             ; the cartridge stopped answering mid-way
    jp      BasDone

; -----------------------------------------------------------------------------
; BasName -- one directory entry on a line of its own.
;
;   in   the entry at the base of the exchange buffer: the attribute byte, bit 0
;        set for a directory, then the name with its NUL
;
; The marker field -- "<DIR> " or the same width in blanks -- and then the name,
; cut at DIR_NAME_MAX rather than printed to its NUL, with nothing padded after
; it. Nothing the cartridge sends reaches the cut (see DIR_NAME_MAX); the
; emulator's long names do, and a name that ran on past the field would wrap to
; a second row and put the page count out by one.
;
; B is the width the name still has, held across CHPUT, which is documented to
; preserve it -- see the printing section below.
; -----------------------------------------------------------------------------
BasName:
    ld      hl, MsgBlank
    ld      a, (CART_BUF)
    bit     0, a
    jr      z, .mark
    ld      hl, MsgDir
.mark:
    call    BasPutz
    ld      hl, CART_BUF + 1
    ld      b, DIR_NAME_MAX
.char:
    ld      a, (hl)
    inc     hl
    or      a
    jp      z, BasEol               ; its ret goes back to BasName's caller
    call    BasPutc
    djnz    .char
    jp      BasEol                  ; the field is full: the rest is not shown

; -----------------------------------------------------------------------------
; BasPause -- hold the listing at the end of a page until a key is pressed.
;
; Released first, then pressed. The RETURN that ran the statement can still be
; down when the first page fills, and the key that released one page can still
; be down when the next one fills a fraction of a second later; a pause that
; waited only for a press would be spent by a key that was already held, which
; is a listing scrolling past the screen it stopped for.
;
; Any key will do -- the only answer this asks for is "go on" -- so the whole
; matrix is scanned rather than one row of it.
; -----------------------------------------------------------------------------
BasPause:
    call    BasKeys
    inc     a                       ; $FF -> 0: nothing at all is held down
    jr      nz, BasPause
.press:
    call    BasKeys
    inc     a
    jr      z, .press
    ret

; The whole keyboard matrix ANDed into one byte: $FF when no key is down.
;
; The row number and the running AND live in window scratch rather than in
; registers, because SNSMAT is a BIOS entry and this file does not assume a BIOS
; entry preserves what it does not document. CHPUT is the one exception, and
; that one is documented -- see the printing section.
BasKeys:
    ld      a, $FF
    ld      (BasKeyBits), a
    ld      a, KEY_ROWS - 1
.row:
    ld      (BasRow), a
    call    SNSMAT                  ; A = that row, a 0 bit per key held down
    di                              ; it hands them back too -- and this covers
                                    ; only the gaps between calls: see BasPutc
    ld      hl, BasKeyBits
    and     (hl)
    ld      (hl), a
    ld      a, (BasRow)
    dec     a
    jp      p, .row                 ; down past row 0 is $FF, which is negative
    ld      a, (BasKeyBits)
    ret

; -----------------------------------------------------------------------------
; BasMail -- one mailbox command through MailCmd in MSX RAM.
;
;   in   A = command code
;   out  A = status, 0 on success
;
; A subroutine rather than a call at each site, so that BasEntry has one operand
; to fill in instead of one per statement. The poll itself cannot be here at
; all: the statement commands the firmware answers with /WAIT released leave it
; serving no bus, so a poll fetched from this window would be reading its own
; opcodes off an undriven one.
;
; A jump, not a call and a ret: MailCmd's own ret then returns to this routine's
; caller, which is what a call and a ret would have arrived at anyway. One byte
; and 21 T-states per command, and a listing sends one command per directory
; entry.
; -----------------------------------------------------------------------------
BasMail:
    jp      0
BasMailVec      EQU $-2

; -----------------------------------------------------------------------------
; BasArg -- the argument: an optional ( "[X:]path" ).
;
;   in   HL = BASIC's text pointer, just past the statement name
;   out  carry set   = this is not an argument of ours. Nothing has been sent,
;                      and the caller chains, so BASIC reports the syntax error
;                      it reports for any malformed statement. There is no way
;                      to raise one from here: that means CALBAS, and CALBAS
;                      switches page 1 out from under this very code.
;        carry clear = HL is past the closing ")", or on the statement's
;                      terminator when there was no argument at all. The drive
;                      is in P_DRIVE and the path at CART_BUF, NUL-terminated;
;                      BasMsg is the line to print instead of sending anything,
;                      or zero; BasPath is where the path starts in BASIC's
;                      text, past the drive prefix, or an empty literal's
;                      closing quote when there was no argument. Only CALL ODIR
;                      reads it -- its header prints the path from the text,
;                      because the copy at CART_BUF is write-only from here:
;                      the buffer answers reads only while a reply has it
;                      open, and the open's reply is a status alone.
;
; No argument at all is drive A and an empty path. That is what CALL OEJECT
; means, and for CALL OMOUNT the empty path reaches the firmware as a file that
; cannot be opened -- which is the truth, reported in the same words as any
; other name that does not exist.
; -----------------------------------------------------------------------------
BasArg:
    xor     a
    ld      (P_DRIVE), a            ; drive A unless a prefix says otherwise
    ld      (CART_BUF), a           ; and an empty path unless one is copied
    ld      (BasMsg), a
    ld      (BasMsg + 1), a
    ld      de, MsgNoPath           ; and no literal unless one is found
    ld      (BasPath), de

    call    BasSpace
    cp      '('
    jr      z, .literal

    ; Nothing but the end of the statement may follow the name. Junk is a
    ; syntax error, and BASIC's is the right one to report.
    or      a
    ret     z                       ; end of the line
    cp      ':'
    ret     z                       ; next statement on the line
    scf
    ret

.literal:
    inc     hl
    call    BasSpace
    cp      '"'
    jr      nz, .bad
    inc     hl

    ; An "A:" or "B:" prefix names the drive. Taken here, before the copy,
    ; rather than by moving the string down over it afterwards.
    ld      a, (hl)
    or      a
    jr      z, .bad                 ; the line ended inside the literal
    inc     hl
    ld      a, (hl)
    dec     hl
    cp      ':'
    jr      nz, .copy
    ld      a, (hl)
    inc     hl
    inc     hl                      ; past the "X:"
    call    BasDrive

.copy:
    ld      (BasPath), hl           ; the path as typed, for CALL ODIR's header
    ld      de, CART_BUF
    ld      b, BAS_PATH_MAX
.byte:
    ld      a, (hl)
    inc     hl
    or      a
    jr      z, .bad                 ; the line ended inside the literal
    cp      '"'
    jr      z, .closed
    inc     b
    dec     b                       ; room left? (tests B without touching A)
    jr      z, .long
    ld      (de), a
    inc     de
    dec     b
    jr      .byte

.long:
    ; Past what the firmware will take. The scan goes on anyway -- HL has to end
    ; past the ")" whatever the answer is -- but nothing more is stored: a path
    ; this long cannot name a file the volume could open, and truncating it
    ; silently is how a 21-character diskrom= became a 15-character one that
    ; opened a different dump.
    push    hl
    ld      hl, MsgNotFound
    ld      (BasMsg), hl
    pop     hl
    jr      .byte

.closed:
    xor     a
    ld      (de), a                 ; terminate the path
    call    BasSpace
    cp      ')'
    jr      nz, .bad
    inc     hl
    or      a                       ; carry clear: parsed
    ret

.bad:
    scf
    ret

; Skip spaces. Answers with the first byte that is not one, in A, and HL
; pointing at it. BASIC keeps the text of a statement exactly as it was typed,
; so the spaces a user puts around the brackets are still here.
BasSpace:
    ld      a, (hl)
    cp      ' '
    ret     nz
    inc     hl
    jr      BasSpace

; -----------------------------------------------------------------------------
; BasDrive -- the letter before the ":".
;
;   in   A = the letter, HL = the text pointer (preserved)
;   out  P_DRIVE set, or BasMsg pointed at "NO SUCH DRIVE"
;
; Refused here rather than by the firmware, which would answer ST_NOTREADY for
; it -- and that is what an empty drive says. "The drive is empty" and "there is
; no such drive" are different answers, and only this side knows which one a
; letter deserves.
; -----------------------------------------------------------------------------
BasDrive:
    ; A string literal reaches us exactly as it was typed, lower case included,
    ; so fold the letter rather than insisting on a capital.
    cp      'a'
    jr      c, .upper
    sub     'a' - 'A'
.upper:
    cp      'A'
    jr      c, .refuse
    cp      'B' + 1
    jr      nc, .refuse
    sub     'A'
    ld      (P_DRIVE), a
    ret

.refuse:
    push    hl
    ld      hl, MsgNoDrive
    ld      (BasMsg), hl
    pop     hl
    ret

; -----------------------------------------------------------------------------
; Printing, in four pieces because CALL ODISK and CALL ODIR build their lines out
; of parts where the statements above print whole ones.
;
;   BasPrint  a NUL-terminated line, ended with CR LF
;   BasEol    just the CR LF
;   BasPutz   a NUL-terminated string, leaving HL past its terminator -- which
;             is how the drive report walks from one record to the next
;   BasPutc   one character in A, with HL kept
;
; CHPUT is in page 0, so all of this runs with the window still open and page 1
; still ours.
;
; CHPUT is documented to preserve every register, and the statements above rely
; on it: the drive report keeps a record pointer in DE and a state byte in C
; across whole lines of printing, and the listing keeps the width a name still
; has in B. That reliance is worth stating
; because it is the one place this file trusts the BIOS with anything --
; everywhere else it assumes an entry destroys what it does not promise, which
; is why SNSMAT's caller keeps its counters in memory.
;
; HL is pushed here all the same: it is one byte of stack and one instruction
; against a documented guarantee that nobody here can check -- and HL is the
; register every routine here walks a string with, so those two bytes cover the
; one thing a guarantee gone wrong would corrupt without ever printing anything
; odd.
; -----------------------------------------------------------------------------
BasPrint:
    call    BasPutz
BasEol:
    ld      a, 13
    call    BasPutc
    ld      a, 10
    jr      BasPutc                 ; its ret goes back to BasEol's caller

; Walked to a NUL and to nothing else, where the parse side deliberately bounds
; the symmetric walk. The two are not inconsistent: what the parser reads is a
; BASIC line, whose length is whatever a user typed, while every string this
; prints out of the exchange buffer was put there by the firmware, which
; terminates each one and stops at DISK_BASIC_PATH_MAX bytes before it does --
; see CmdBasicInfo and CmdBasicDirNext. A bound here would be a second opinion
; about a length the other side already fixed.
BasPutz:
    ld      a, (hl)
    inc     hl
    or      a
    ret     z
    call    BasPutc
    jr      BasPutz

BasPutc:
    push    hl
    call    CHPUT
    ; Interrupts off again, because CHPUT hands them back. The BIOS does not
    ; save and restore that flag: SETRD and SETWRT bracket their two writes to
    ; the VDP's address port with a di and an unconditional ei, so every entry
    ; that reaches VRAM returns with interrupts enabled whatever it was called
    ; with -- and CHPUT reaches VRAM on every character.
    ;
    ; What this di does and does not buy, exactly, because it is not what a
    ; reader expects. It does NOT keep the statement under di: a Z80 accepts an
    ; interrupt after the instruction FOLLOWING an ei, so from that ei inside
    ; SETWRT onwards -- the rest of SETWRT, and then everything CHPUT still has
    ; to do, which for a character that wraps the line or scrolls the screen is
    ; a whole-screen LDIRVM -- runs with interrupts enabled and the window still
    ; open. Nothing placed after the call can shorten that, and a ret and a di
    ; cannot be made one instruction. And the VDP holds /INT until its status
    ; register is read, so a tick that arrived while this blob was under di is
    ; not missed: it is taken at that ei, inside the call, every time.
    ;
    ; That is why a di is not what keeps the open window safe. The tick runs
    ; the BIOS handler, and the handler calls H.TIMI, and on both supported
    ; dumps H.TIMI is the Disk ROM's motor-off timer at 0x7837 (National) or
    ; 0x7827 (Yamaha) -- inside this window, so the CALLF lands on this blob's
    ; bytes and the machine hangs -- CALL ODIR would hang every time and CALL
    ; ODISK often, by the dumps' own bytes, not measured on a machine. What
    ; keeps the window safe is BasEntry writing a ret over H.KEYI and H.TIMI
    ; for the length of the statement, so the handler that the ei lets in
    ; never leaves page 0 and RAM; both exits put the hooks back.
    ;
    ; What this di still buys, with the hooks a ret: the exposure to the BIOS's
    ; own handler stops at the call instead of outliving it. Without the di the
    ; statement runs with interrupts on from its first printed character to
    ; BasicClose -- the mailbox round trips, the listing loop and the paging
    ; pause included, which for a directory listing is most of a second -- and
    ; every one of those would then be interleaved with keyboard scans and
    ; whatever else the handler does. With it, the handler runs only inside the
    ; BIOS calls that let it in, and nothing between them. Two bytes, and the
    ; blob's own invariant stays simple: outside a BIOS call, nothing runs but
    ; this code. BasicClose's ei is what gives BASIC its interrupts back.
    di
    pop     hl
    ret

; -----------------------------------------------------------------------------
; The chain. The name was not ours, so the vendor's own statement handler gets
; it, with HL and PROCNM exactly as BASIC left them.
;
; The vendor's entry goes on the stack rather than into a jp, because the jump
; must not happen until the window is shut and shutting it is the trampoline's
; job -- a vendor handler that happened to live inside BAS_BASE..BAS_BASE+511
; would otherwise be entered on our bytes instead of its own. So BasicChain's
; ret is what lands in the vendor's handler, on top of the return address BASIC
; pushed, and the vendor's own ret then goes back to BASIC the way it always
; did. That is a "jp (vendor)" with the window closed in the middle of it.
;
; A dump that published no extended statements leaves the cell zero. Then
; nothing is pushed, the same ret goes straight back to BASIC, and the carry
; BasicChain sets says "not mine" -- a name refused, which BASIC reports as a
; syntax error, rather than a jump to address 0.
;
; A and DE are scratch on the way through. What BASIC's dispatcher sets up is
; PROCNM and HL, and both reach the vendor exactly as they were.
; -----------------------------------------------------------------------------
BasChain:
    ld      hl, (TRAMP_BASE_PTR)
    ld      de, BAS_ORIG_OFS
    add     hl, de
    ld      e, (hl)
    inc     hl
    ld      d, (hl)                 ; DE = the vendor's STATEMENT address
    ld      a, d
    or      e
    jr      z, .leave               ; nothing published: leave without pushing,
                                    ; which turns BasicChain's ret into the
                                    ; refusal BASIC wants
    push    de
.leave:
    ld      hl, (BasText)           ; exactly as BASIC left it
    jp      BasGoChain

; The two ways out, reached through their operands because neither address is
; known until the INIT tail has reserved its RAM. Both filled in at BasEntry.
;
; Every exit is one of these two: a handled statement, a printed refusal, a
; failed command and a timeout all reach BasDone and so BasGoRet; a foreign
; name and a malformed argument reach BasChain and so BasGoChain. Nothing jumps
; to a trampoline address by any other route, which is what makes this the
; place to put the hooks back -- a future exit cannot forget what it cannot
; avoid. Still under di here, so the restore is atomic against a tick, and the
; chain path restores BEFORE the vendor's handler is entered, so a vendor
; statement that reads or rewrites its own hook sees it intact.
BasGoChain:
    call    BasHooksBack
    jp      0
BasChainVec     EQU $-2

BasGoRet:
    call    BasHooksBack
    jp      0
BasRetVec       EQU $-2

; The bytes BasEntry saved go back over the two hooks. A alone is used: HL is
; already BASIC's text pointer by the time either exit calls this, and the
; carry the trampoline sets is set after it.
BasHooksBack:
    ld      a, (BasKeyiSave)
    ld      (H_KEYI), a
    ld      a, (BasTimiSave)
    ld      (H_TIMI), a
    ret

; --- the statements we answer to ---------------------------------------------
; Counted name, then the handler. The count is what keeps a name from matching
; a longer one; the terminator test at BasEntry.char is what keeps a longer one
; from matching a name. A zero length ends the table.
;
; The addresses are absolute and inside this blob, which is assembled at
; BAS_BASE and served there -- nothing relocates it, and build_stub.py checks
; that the bytes come out identical at any STUB_ORG.
BasNames:
    db      4, "ODEL"
    dw      BasOdel
    db      5, "OCOPY"
    dw      BasOcopy
    db      4, "ONEW"
    dw      BasOnew
    db      6, "OMOUNT"
    dw      BasMount
    db      6, "OEJECT"
    dw      BasEject
    db      5, "ODISK"
    dw      BasDisk
    db      4, "ODIR"
    dw      BasDir
    db      0

; --- what a statement can say ------------------------------------------------
; An error prints a line and returns as handled. A real BASIC error would mean
; CALBAS, which switches page 1 out from under code that is executing there.
MsgExists:     db "FILE EXISTS", 0
MsgInUse:      db "IMAGE IN USE", 0
MsgFull:       db "DISK FULL", 0
MsgWriteProtected: db "WRITE PROTECTED", 0
MsgInvalid:    db "INVALID ARGUMENT", 0
MsgIOError:    db "DISK I/O ERROR", 0
MsgNotFound:    db "FILE NOT FOUND", 0
MsgNoDrive:     db "NO SUCH DRIVE", 0
MsgNoAnswer:    db "CARTRIDGE NOT RESPONDING", 0

; And what CALL ODISK says about a drive rather than about a failure. The gap
; before "(protected)" is inside that string rather than printed separately, so
; that the common case -- a writable image -- emits nothing extra at all.
MsgEmpty:       db "-- empty --", 0
MsgProtected:   db "  (protected)", 0

; And what CALL ODIR marks a directory with, against the same width in blanks for
; a file, so that the name starts in the same column either way. The gap between
; the marker and the name is inside the string, as "(protected)"'s is, so that
; an entry costs one string and no separate space. The ASSERTs are what keep a
; marker edited to a different word from quietly moving the names' column.
MsgDir:         db "<DIR> ", 0
MsgBlank:       db "      ", 0
    ASSERT MsgBlank - MsgDir == DIR_MARK_COLS + 1
    ASSERT $ - MsgBlank == DIR_MARK_COLS + 1

; And where CALL ODIR's header starts when the statement had no argument: the
; closing quote of a literal nobody typed, so that the walk to the closing quote
; ends at once and the root lists as "/" alone. See BasArg.
MsgNoPath:      db '"'

; --- blob scratch ------------------------------------------------------------
; Inside the window, so these are bytes of the firmware's exchange buffer
; rather than of MSX RAM. A read command fills at most MAX_SECTORS sectors,
; which stops below BAS_BASE, so sector data can never reach them.
BasText:        dw 0        ; BASIC's text pointer, as it arrived
BasMsg:         dw 0        ; a line to print instead of sending a command
BasCmd:         db 0        ; which command this statement is
BasPath:        dw 0        ; where the path literal starts in BASIC's text
BasPage:        db 0        ; lines left on this page before the listing pauses
BasRow:         db 0        ; the keyboard row that pause is scanning
BasKeyBits:     db 0        ; the rows it has scanned so far, ANDed together
BasKeyiSave:    db 0        ; H.KEYI's first byte, while a ret stands in for it
BasTimiSave:    db 0        ; H.TIMI's, likewise -- see BasEntry

BasEnd:
    ; The firmware serves BAS_SIZE bytes from BAS_BASE, so anything past this
    ; point is outside the window and would read back as vendor ROM.
    ASSERT BasEnd - BAS_BASE <= BAS_SIZE
    ENDIF
