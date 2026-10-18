# OmniSCC commands for MSX-DOS

Seven independent Z80 programs call the same cartridge API as BASIC's `CALL
OMOUNT`, `CALL OEJECT`, `CALL ODISK` and `CALL ODIR`, plus the outer-volume file
commands below. They run after OmniSCC has booted a disk image and installed
its disk interface. No BASIC program is required. File operations require
firmware with commands `40h`–`43h`. Each `.asm` is a separate command; shared
argument parsing, inter-slot access and console routines are in `common.inc`.

## Build

Standalone, with CMake and sjasmplus (no Pico SDK or ARM compiler):

```sh
cmake -S src/msx/dos -B src/generated/dos -G Ninja
cmake --build src/generated/dos
```

The output directory contains `OMOUNT.COM`, `OEJECT.COM`, `ODISK.COM`, `ODIR.COM`,
`ODEL.COM`, `OCOPY.COM` and `ONEW.COM`. To choose the assembler, pass
`-DSJASMPLUS_EXECUTABLE=/path/to/sjasmplus` when configuring, or set `SJASMPLUS`.
On Windows the default is the bundled `../sjasmplus.exe`.

The firmware's normal CMake build also builds all seven programs, into its
`generated/build/dos/` directory; they can be built separately with target `dos_commands`.
To assemble just one source, for example from the repository root:

```sh
mkdir -p src/generated/dos
(cd src/msx/dos && sjasmplus --raw=../../generated/dos/OMOUNT.COM omount.asm)
```

Copy the `.COM` files **inside an MSX-DOS disk image**, where DOS can run them.
Copying them onto the cartridge's outer FAT volume alone does not put them
inside either emulated floppy.

## Command names and example disk

The firmware uses identical `O`-prefixed names in BASIC (`CALL OMOUNT`, etc.)
and DOS. The old MOUNT/EJECT/DISK executables and MOUNT/EJECT/DISK/DIR BASIC
names are replaced. All API command numbers remain unchanged.
`tools/OmniSCC.dsk` includes all seven tools and standard MSX-DOS 1.03
with COMMAND.COM 1.11, requiring 64 KiB RAM.

## Commands

```text
OMOUNT GAME2.DSK
OMOUNT B:SIDE-B.DSK
OMOUNT "B:Games/Metal Gear.dsk"
OEJECT B:
ODISK
ODIR
ODIR GAMES
ODEL OLD.DSK
OCOPY "Games/Original.dsk" "Games/Backup.dsk"
ONEW EMPTY.DSK
ONEW SINGLE.DSK 360
```

- `OMOUNT [A:|B:]path` replaces the image in a cartridge drive. The default is
  cartridge drive A, regardless of DOS's current drive. Paths refer to the
  cartridge's outer FAT volume, not to files inside the mounted image.
- `OEJECT [A:|B:]` leaves that drive empty; the default is A. Both media-changing
  commands flush DOS's disk buffers before issuing the cartridge command.
- `ODISK` reports both drives, including their full paths, sizes in KiB (`K`),
  and read-only state. A size with an odd number of sectors includes `.5K`.
- `ODIR [path]` lists directories and `.dsk` images on the outer volume; no
  argument lists its root. An optional `A:`/`B:` prefix is ignored, as in
  BASIC's `CALL ODIR`. The different name avoids DOS's built-in `DIR` command.
  Output scrolls without paging; Esc or Ctrl-C stops the listing between
  entries and closes its directory handle.

- `ODEL path.dsk` removes an image from the outer volume. A mounted image
  must be ejected first, including when addressed by its short 8.3 alias.
- `OCOPY source.dsk destination.dsk` copies the complete image. The destination
  must not exist. DOS buffers and mounted image handles are flushed first;
  close application files before copying an image in use.
- `ONEW path.dsk [360|720]` creates an empty formatted FAT12 image. Size is in
  KiB, default 720; 360 is also supported. It neither installs DOS system files
  nor mounts the new image. Existing files are never overwritten.

The three file operations use outer-volume paths without `A:`/`B:` prefixes,
require `.dsk`, and reject wildcards and `.`/`..` components. Copy/new transfer
one sector per mailbox round trip. On an ordinary error they remove the partial
output; power loss or persistent media failure can leave it behind.

Use quotes around paths containing spaces. OMOUNT and OEJECT paths are limited to
64 bytes excluding the optional drive prefix and quotes; ODEL, OCOPY and ONEW
use the same 64-byte limit without a drive prefix. Extra arguments, invalid
prefixes and unterminated quotes print usage without sending any API command.
`/?` prints usage. Console output uses DOS function 06h, so `$` in a name is
printed literally and Ctrl-C cannot bypass the listing's cleanup.

Example `ODISK` output:

```text
A: GAME2.DSK  720K
B: -- empty --
```

The letters A/B identify OmniSCC's two drives, not a global DOS drive mapping
when other disk interfaces are installed. If several matching OmniSCC
interfaces exist, the first in primary-slot/subslot order is selected.
Changing or ejecting DOS's system disk can make its next access to
`COMMAND.COM` fail; keep the system disk available or use drive B for swaps.
Do not change media with files still open: flushing DOS buffers does not close
an application's files. A rejected `OMOUNT` leaves the old image mounted, as
defined by the cartridge API.

## API and slot access

The implementation uses the [disk API](../../docs/omniscc_api.md):
`40h` delete, `41h` copy, `42h` create, `43h` transfer step,
`3Bh` mount, `3Ch` eject, `3Dh/3Eh` directory iteration, and `3Fh` information.
It closes each reply overlay with `00h` before re-enabling interrupts or
calling DOS. Directory completion, cancellation and errors additionally issue
`3Ah`, which closes the firmware's directory handle. It never invokes the
BASIC handler or an internal driver subroutine.

Discovery is read-only. The current firmware has no dedicated identification
command: the client checks the `AB` ROM header, the published `7800h` statement
vector, and the adjacent DSKIO/DSKCHG RAM trampoline destinations. This is a
recognition of the current driver layout, not a universal cartridge signature;
a firmware that changes that layout will require updating the probe. The
INIT-vector bytes are not read, avoiding the firmware's boot-scan detector.

Every access uses DOS's `RDSLT`/`WRSLT` entries, preserving registers according
to the [MSX2 Technical Handbook, section 7.2](https://www.konamiman.com/msx/msx2th/th-5b.txt).
Code, arguments and replies reside in the program's page-0 RAM. DOS restores
the original slots on return from each inter-slot access; the programs do not
change the RAM mapper or install interrupt hooks.

During a transaction interrupts stay disabled. Readiness requires `A5h` at
`7FFFh` and a different value at `7FFEh`, which is the **disk** protocol (the
loader protocol reverses these registers). The wait is bounded to 65536
inter-slot polls. On timeout the utility reports it and returns to DOS without
issuing another command to firmware which may still be busy; completion of
the timed-out operation, including handle cleanup, cannot be guaranteed.

Only DOS 1-compatible calls are used. Programs exit through address `0000h`;
errors are printed, with no DOS 2-specific errorlevel. Buffer flushing and
console I/O follow the [MSX-DOS function specification](https://map.grauw.nl/resources/dos2_functioncalls.php).

## Hardware verification

Before a release, run the commands under real MSX-DOS on the cartridge: mount and
eject B, confirm DOS sees the media change, list the root and a subdirectory,
check a missing path, and cancel a long listing. Repeat with an expanded slot
and both DOS versions where available.
