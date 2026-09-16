# OmniSCC Firmware

Firmware for the OmniSCC — an RP2040-based MSX multi-cartridge that loads ROM
and `.dsk` floppy images from the Pico's external 16 MB flash. In an MSX it
emulates plain and mapped ROM cartridges, the Konami SCC sound chip, and floppy
drives backed by disk images. The browser menu lets you select a ROM or mount a
disk image; an alternative menu offers ten configurable favourites.

When powered from USB without the MSX, the cartridge appears as a removable
drive for copying images by drag-and-drop. Disk emulation requires a separate
16 KB MSX Disk ROM dump supplied by the user; see [Disk images](#disk-images).

**Important:** Cartridge timing is sensitive. Always build and run a
**Release** firmware for real MSX use. Debug builds will not keep up with the
Z80 bus.

For the hardware itself — board variants, jumpers, enclosure — see the
[repository README](../README.md).

---

## Prerequisites

- Raspberry Pi Pico SDK v2.2.0 or newer
  - Set the environment variable `PICO_SDK_PATH` to the SDK root.
- ARM embedded GCC toolchain (`arm-none-eabi-gcc` and `arm-none-eabi-g++`
  on `PATH`). The Pico SDK selects GCC by default. To use a compatible ARM
  Clang toolchain, configure a separate build directory with
  `-DPICO_COMPILER=pico_arm_clang`. GCC builds use newlib-nano (`nano.specs`)
  to leave enough RP2040 RAM for cartridge allocation; install the toolchain
  package containing that library.
- `ninja` build tool on `PATH`
- Python 3 on `PATH` — required at configure time and used during the build
  to run `msx/build_stub.py`, which assembles and relocates the disk driver stub.
- (Optional) OpenOCD + CMSIS-DAP debugger if you want to flash with `ninja flash`
- Z80 assembler `sjasmplus` for building the MSX loader ROM
  - A Windows binary (`msx/sjasmplus.exe`) is included in this repo.
  - On Linux, install native `sjasmplus` on `PATH`. For a custom location,
    pass `-DSJASMPLUS_EXECUTABLE=/path/to/sjasmplus` to CMake and set
    `SJASMPLUS=/path/to/sjasmplus` when running the Python tools or host tests.

---

## Building the firmware

All build outputs go under the ignored `generated/` directory:

```bash
cmake -S . -B generated/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build generated/build
```

### Board variants

The board header selects which hardware the firmware is built for. It defaults
to the base board; pass `-DPICO_BOARD=omniscc_plus` for OmniSCC+ (level
shifters):

```bash
# base board (default)
cmake -S . -B generated/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=omniscc

# OmniSCC+ — defines WITH_SHIFTERS
cmake -S . -B generated/build-plus -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=omniscc_plus
```

The headers live in `include/boards/`. Both also set
`PICO_FLASH_SPI_CLKDIV 4`, which is safe for the slower ZBIT flash found on
most Pico clones; if your module carries Winbond flash you can switch to the
commented-out `2` for a faster boot.

The CMake project is named `OmniSCC`, so the build produces:

- `generated/build/OmniSCC.uf2` — drag-and-drop firmware image
- `generated/build/OmniSCC.elf` — ELF with debug symbols
- `generated/build/OmniSCC.bin` — raw binary
- `generated/build/OmniSCC.hex`, `generated/build/OmniSCC.dis`, `generated/build/OmniSCC.elf.map`

Prebuilt images for both variants are also committed in [`../firmware/`](../firmware/)
(`OmniSCC.uf2`, `OmniSCC+.uf2`).

The Z80 preloader (`msx/preloader.asm`) and both menu ROMs are built
automatically by CMake: `msx/loader.asm` → `generated/loader.rom` (the browser) and
`msx/loader_fav.asm` → `generated/loader_fav.rom` (ten favourites), 8 KB each. Copy
**one** of them to the root of the cartridge's USB drive as `loader.rom` after
the first boot (see below).

---

## Flashing the firmware to a new device

### Method 1: UF2 drag-and-drop

1. Hold the **BOOTSEL** button on the Pico.
2. Plug the Pico into USB while keeping BOOTSEL held.
3. Release BOOTSEL. A removable drive named **RPI-RP2** appears.
4. Copy `generated/build/OmniSCC.uf2` (or an image from `../firmware/`) to that drive.
5. The Pico reboots automatically.

### Method 2: OpenOCD (CMSIS-DAP debugger)

If OpenOCD is available, CMake creates a `flash` target:

```bash
cmake --build generated/build --target flash
```

---

## Preparing the cartridge for the first MSX boot

After flashing, the device boots into USB device mode when no MSX slot-select
signal is detected:

1. Plug the cart into USB **without** the MSX powered on.
2. A removable drive named **MSXPico** appears.
3. On the very first boot the firmware formats the internal flash and creates
   `WELCOME.TXT`.
4. Copy the following files to the drive root:
   - `loader.rom` (built above)
   - One or more MSX `.rom` files you want to run
   - For disk images: your own `disk.rom` — a 16 KB dump of an MSX Disk ROM —
     plus the `.dsk` images themselves (see below)
5. Optionally create `/loader.ini` to customize the menu (see below). The
   browser creates it when you boot an entry.
6. Safely eject the drive and plug the cart into the MSX.

### Disk images

A `.dsk` image is served as a floppy drive once you pick it from the menu. Any
file whose size is a whole number of 512-byte sectors is accepted; 360 KB and
720 KB images are what the vendor Disk ROM recognises. The Disk ROM is yours to
supply — the firmware patches its low-level driver entries and nothing
copyrighted ships with the project — and it must be named `disk.rom`.

Images are **writable**: `SAVE`, `COPY` and anything else the disk software
does lands in the image file on the cartridge's own flash. Two things follow.

- **To protect an image, set its read-only attribute** on the host before
  ejecting the drive (`attrib +r` on Windows, `chmod a-w` elsewhere). The MSX
  is then told the disk is write protected, so Disk BASIC refuses a `SAVE`
  instead of failing it halfway.
- Writes are committed at the end of each disk call, not batched until you
  return to the menu, so pulling the power a moment after a `SAVE` reports
  success loses nothing. Pulling it *during* one loses that call's sectors.

`CALL FORMAT` works on 360 KB and 720 KB images, on either drive: it asks for
the drive letter, then writes a fresh boot sector, FATs and an empty root
directory to that drive's image, leaving the data area alone the way a real floppy
format does. No format choice is offered, because an image's size decides its
layout — Disk BASIC goes straight from the drive letter to "Strike a key when
ready". Images of other sizes still mount and read, but cannot be formatted — the
firmware will not invent a BPB for them.

If you own more than one machine, pair an image with the right dump using
`diskrom=` in `loader.ini` (see the next section): the National and Yamaha Disk
ROMs differ both in Disk BASIC and in where their controller registers sit.

[`../tools/`](../tools/) contains prebuilt browser and favourites
menu ROMs, plus a sample `loader_fav.ini` for the favourites menu. Copy the
chosen menu ROM onto the cartridge as `loader.rom`.

> **Tip:** The firmware uses FatFs with long-filename and exFAT support, but
> keeping filenames short and without spaces avoids any potential issues with
> the simple Z80-side file parser.

---

## Disk commands from MSX-DOS

`MOUNT.COM`, `EJECT.COM`, `DISK.COM`, `ODIR.COM`, `ODEL.COM`, `OCOPY.COM` and
`ONEW.COM` expose the BASIC extensions as DOS commands through the same
cartridge API. Each has its own
assembly source in [`msx/dos/`](msx/dos/README.md). They build with the firmware
into `generated/build/dos/`, or independently without the Pico SDK:

```sh
cmake -S msx/dos -B generated/dos -G Ninja
cmake --build generated/dos
```

Put the COM files inside a DOS disk image, then use e.g. `MOUNT B:GAME.DSK`,
`EJECT B:`, `DISK`, `ODIR GAMES`, `ODEL OLD.DSK`,
`OCOPY SOURCE.DSK BACKUP.DSK` or `ONEW EMPTY.DSK`. Paths name files on the
cartridge's outer volume. `ONEW` defaults to a 720 KiB FAT12 image and accepts
`360` for a 360 KiB image. `ODEL` refuses mounted images; `OCOPY` and `ONEW`
never overwrite existing files. See the [command guide](msx/dos/README.md) for
quoted paths, limits, slot discovery, media changes, and verification.

## Disk commands from BASIC

Once a `.dsk` image is running, four cartridge-published statements —
`CALL MOUNT`, `CALL EJECT`, `CALL DISK` and `CALL DIR` — change or show what is
mounted without a reboot. They work the same way `CALL FORMAT` already does:
nothing to load, they are just there at the Disk BASIC prompt.

```basic
CALL MOUNT("GAME2.DSK")        ' replaces what is in drive A
CALL MOUNT("B:SIDE-B.DSK")     ' replaces what is in drive B
CALL EJECT("B:")               ' empties drive B; no argument means A
CALL DISK                      ' what is mounted in A and B
CALL DIR                       ' the cartridge volume's root
CALL DIR("GAMES")              ' a subdirectory of it
```

The outer-volume image commands are also available from BASIC:

```basic
CALL ODEL("OLD.DSK")
CALL OCOPY("SOURCE.DSK","BACKUP.DSK")
CALL ONEW("EMPTY.DSK")           ' formatted FAT12, 720 KiB by default
CALL ONEW("SINGLE.DSK",360)      ' 360 or 720 KiB
```

Use literal paths to `.dsk` files on the cartridge volume, without drive
prefixes. Subdirectories are supported. `ODEL` refuses mounted images; eject
them first. `OCOPY` and `ONEW` never overwrite an existing file. Close BASIC
files before copying a mounted image: the cartridge synchronizes its writes,
but cannot flush a BASIC application's open-file buffers. A new image contains
an empty filesystem and zeroed data, with no DOS system files, and is not mounted
automatically. The matching DOS commands are described in [the DOS guide](msx/dos/README.md).

`CALL DISK` prints one line per drive:

```
A: GAME2.DSK  1440K
B: -- empty --
```

with `  (protected)` appended when the mounted image is read-only.

`CALL DIR` lists the cartridge's own FAT volume, not either drive — the
mounted images are files on that volume, and this is how you find one to
mount. It shows only what `CALL MOUNT` can open: directories and `.dsk`
images, nothing else — a ROM, `loader.ini` or `disk.rom` sitting on the same
volume is left out. The first line is the path listed, then one entry per
line, directories marked `<DIR>`:

```
/GAMES
<DIR> KONAMI
<DIR> SAVES
      Metal Gear (1987) (Konami).dsk
      Vampire Killer (1986).dsk
      METALG~2.DSK
```

A name is shown whole when it is 32 characters or fewer; a longer one is
listed by its 8.3 alias (`METALG~2.DSK` above), which `CALL MOUNT` opens just
the same. A line is 38 columns at most, sized for `SCREEN 0`'s 40-column line
(a long name wraps under `SCREEN 1`'s narrower one). The listing pauses every
22 lines, the path line included, for any keypress, with no prompt printed and
no way to cancel it early. Under openMSX the listing differs slightly from
what the cartridge would show — see `emul/README.md` for why.

**The `B:` prefix.** `CALL MOUNT` and `CALL EJECT` take an optional `A:` or
`B:` in front of the path; leaving it off means drive A. Any other letter is
refused on the spot with `NO SUCH DRIVE`, before anything reaches the
cartridge. `CALL DIR` goes through the same parser, so it accepts an `A:` or
`B:` prefix too and then ignores it — the volume it lists has no drives of its
own, so `CALL DIR("B:GAMES")` lists `GAMES` just as `CALL DIR("GAMES")` does.

**The argument is a literal, not an expression.** `CALL MOUNT(F$)` or
`CALL MOUNT("B:" + D$)` is a syntax error: evaluating a variable would mean
running BASIC's own expression evaluator while this code is still executing
out of the cartridge's page 1, which needs `CALBAS` and that switches page 1
out from under it. Use `CALL DIR` to find the name, then type it as a literal.

**Changing a disk with a file open on it loses whatever was buffered** —
exactly like pulling a real floppy out while Disk BASIC still has one open.
`CALL MOUNT` and `CALL EJECT` do not check for an open file first; that is
stated here rather than prevented. A `CALL MOUNT` that fails — a name with no
file behind it, a directory, or a file that is not a whole number of 512-byte
sectors — leaves the drive's current disk in place and prints
`FILE NOT FOUND`; a typo does not eject the running disk.

These four statements are implemented and pass the project's host-side test
suite. The bus pattern the firmware relies on to open the handler's window —
BASIC's `CALL` dispatch reading `0x4004` then `0x4005` and entering the vector
on the very next cartridge cycle — has been measured in openMSX on both
supported Disk ROM dumps, and the sniffer matches it (the logs are in the
design spec's `## Measured` section). All four statements have since been run
end to end in openMSX (`CALL DISK`, `CALL DIR` in the root and a subdirectory,
`CALL MOUNT`/`CALL EJECT` for drive B, `CALL FORMAT` chaining to the vendor,
`CALL FOO` refused with `Syntax error`), with the interrupt-hook restore
confirmed working on every exit. On 2026-09-15 the user reported the four
statements working on real hardware, with `SCCMUZAX.DSK` and the other disk
images booting against the 301-byte reservation — reported working, not
verified item by item; the per-statement hardware checklist (`CALL DIR`
within seconds of a disk access, ten `CALL DISK`s in a row) was not run
individually. Details and dates are in the design spec's `## Measured`
section.

Full command codes, buffer layouts and statuses are in
[`docs/omniscc_api.md`](docs/omniscc_api.md).

---

## Which menu ROM to copy

There are two, and **you copy exactly one of them to the volume as
`loader.rom`** — that is the name the firmware loads, and nothing chooses
between two files.

| Built file | Menu |
|---|---|
| `generated/loader.rom` | A browser over the volume: subdirectories, scrolling, and the last choice remembered. Already the right name — copy it as it is. |
| `generated/loader_fav.rom` | Ten favourites listed in `loader.ini`. Simple, predictable, nothing to browse. Copy it to the volume **as `loader.rom`**. |

They are mutually exclusive in the ini file too: each owns `/loader.ini` and
reads its own keys, so switching from one to the other means writing the file
for the one you kept. The sections below cover the browser first, then the
favourites menu.

## The browser menu (`loader.rom`)

This is what the build produces as `generated/loader.rom`, so copying that file to the
volume is all it takes. The menu is a list of what is actually there: subdirectories first, then `.rom` and `.dsk`
files, sorted alphabetically. Directories are marked `[D]` and drawn in the same
colour as the bottom line rather than white, so they stand out on a monochrome
monitor as well as a colour one, and the column beside the list carries an up or
down arrow when it continues past the window. Nothing else is shown — a `readme.txt` cannot be
booted, and neither can `loader.rom` itself or whatever `diskrom=` names, so
none of them appear.

| Key | Action |
|-----|--------|
| Cursor up / down | Move the selection; the list scrolls when it reaches an edge |
| Cursor left / right | Page up / page down |
| `RETURN` | Enter a directory, or boot a file |
| `BS` | Up one directory — or select the `..` row, which every directory but the root lists first |
| `SPACE` | Put the highlighted `.dsk` in **drive B**, or take it out again if it is already there. The bottom line shows what is in it, and the choice is written to `disk2=` straight away |
| `ESC` | Return to BASIC (mapping the bare SCC first if `scc_on_exit`) |

The seconds left before the saved entry boots by itself are shown at the right of
the hint line, and any key stops the countdown for good.

Booting an entry saves it, so the next power-on offers the same one after the
countdown and the browser opens in the directory it lives in. There is no
separate "save" key.

A filename is shown truncated to the column but stored and booted **whole**, so
a mapper tag in the name — `mg[KonamiSCC].rom` — still reaches the firmware. Only
a name longer than 31 characters falls back to its 8.3 alias, which is the one
case where the full name will not fit.

Filenames like that are not what you want to read in a menu, so a `name=` line
gives one a title: the row then shows the title, the list sorts by it, and the
file underneath is still opened by its real name. See the ini reference below.

Long directories are truncated and the footer says so; how many entries fit is
worked out from the machine's free RAM at startup — minus room for the stack,
which on a machine with a disk interface sits just under that limit — and comes
to around 170 there.

### `loader.ini` reference (browser menu)

The browser owns the file and **rewrites it** every time you boot an entry: five
keys and your `name=` lines, in the order you wrote them. Comments and any other
key are lost on the first boot, which is the price of not needing to edit the
file at all.

```ini
last=/games/mg[KonamiSCC].rom
disk2=/games/side-b.dsk
delay=500
diskrom=/roms/yis805-128r2_disk.rom
scc_on_exit=1
name=mg[KonamiSCC].rom,Metal Gear
name=sd-snatcher[SCC].rom,SD Snatcher
```

| Key | Value | Description |
|-----|-------|-------------|
| `last` | path | The entry booted last. Sets the auto-boot target and the directory the browser opens in. A `.dsk` here mounts as a disk; anything else boots as a ROM. |
| `disk2` | path | Image for drive B, mounted alongside `last` whenever that boots a `.dsk`. Set it with `SPACE` in the browser or by hand. An image that will not open leaves drive B empty rather than failing the boot. |
| `delay` | milliseconds | Countdown before `last` boots automatically. Any keypress cancels it. |
| `diskrom` | path | Disk ROM dump to patch for `.dsk` entries, instead of `disk.rom`. A path, so the dump can live in a folder — `diskrom=/roms/yis805-128r2_disk.rom` — at most 64 characters, and no spaces in it. |
| `scc_on_exit` | `0`/non-zero | If non-zero, `ESC` maps the bare SCC cartridge. |
| `name` | `<filename>,<title>` | Lists that file under `<title>` instead of its filename. Repeat the key once per file; the browser keeps these lines when it rewrites the file. |

`name=` is matched on the **filename alone**, with no path, so the same title
applies to that filename in any directory — which is what makes the lines
writable by hand: you see a name in the browser and write that. Case does not
matter, spaces around the `=` and the comma are ignored, and a title may contain
spaces and commas. Directories can be titled too. The file is still opened by its
real name, so a mapper tag in it keeps working.

A missing or unreadable file is not an error: the browser opens at the root with
a 500 ms delay and nothing to auto-boot. If `last` names a file that has since
been deleted, the mount fails with a message and the countdown is not armed
again, so it cannot loop.

#### Two drives

Booting a `.dsk` mounts `last` as drive A and `disk2` as drive B, so a two-disk
game works without swapping anything. Both are read/write, and each carries its
own write-protect (the file's read-only attribute) and its own disk-changed flag.

Whether the MSX *offers* B: is the vendor Disk ROM's decision, not the
cartridge's: the drive count comes from the dump's own `INIT`, which this
firmware does not patch. A dump that advertises one drive gives you A: alone no
matter what `disk2=` names — most advertise two. Anything past B is refused with
"not ready", and so is a B: with no image, rather than being served drive A's
sectors under a second letter.

---

## The favourites menu (`loader_fav.rom`)

The other menu: ten slots you name yourself, with no directory to walk. Copy
`generated/loader_fav.rom` to the volume **as `loader.rom`** to use it, and write the
ini below — the two schemas share a filename and nothing else.

### `loader.ini` reference (favourites menu)

`/loader.ini` tells the boot menu which ROMs to show and which slot to start.
The parser is simple and **case-sensitive**. Lines starting with `;`, `#` or
`[` are ignored.

```ini
; up to 10 favourites, numbered 0..9
fav0=game1.rom,Game One
fav1=game2.rom
fav2=some_ascii8.rom

; slot highlighted on startup (0..9)
default=0

; auto-boot countdown in milliseconds
delay=3000

; if non-zero, pressing ESC maps the SCC only cartridge
scc_on_exit=1
```

### Keys

| Key | Value | Description |
|-----|-------|-------------|
| `favN` | `filename.rom[,label]` or `filename.dsk[,label]` | Assign a ROM or `.dsk` disk image to menu slot `N` (0–9). A name ending in `.DSK` (case-insensitive) is mounted as a disk image; anything else boots as a ROM. The optional `label` is shown in the menu. |
| `default` | `0..9` | Slot that is highlighted when the menu starts. |
| `delay` | milliseconds | Countdown before the highlighted slot boots automatically. Defaults to 500 ms; `delay=0` is ignored, so the default applies. |
| `diskrom` | `path` | Disk ROM dump to patch when a `.dsk` entry is booted, instead of `disk.rom`. A path, so the dump can live in a folder — `diskrom=/roms/yis805-128r2_disk.rom` — at most 64 characters and no spaces. Applies to every `.dsk` entry in the file. |
| `scc_on_exit` | `0..9` | If non-zero, pressing `ESC` in the menu maps the "SCC only" cartridge. |

The countdown is converted to VBlank frames at ~50 Hz, so it is rounded down to
a multiple of 20 ms. Any keypress cancels it permanently for that session; there
is no value that disables auto-boot outright.

### Navigating the menu

| Key | Action |
|-----|--------|
| Cursor up / down | Move the highlight; empty slots are skipped |
| `RETURN` | Boot the highlighted slot |
| `0`–`9` | Boot that slot directly |
| `ESC` | Return to BASIC (mapping the bare SCC cartridge first if `scc_on_exit` is non-zero) |

### Auto-saving the default slot

When you manually select a slot and boot a ROM, the loader rewrites
`/loader.ini` so the selected slot becomes the new `default=`. Only the single
digit after `default=` is changed; the rest of the file is left untouched.

---

## Supported mappers

The firmware auto-detects the following mapper types:

- Plain / linear ROM
- Konami (8 KB pages)
- Konami SCC
- ASCII8 (4 × 8 KB pages)
- ASCII16 (2 × 16 KB pages)

A sixth mode, **SCC only**, is not detected from a ROM: it is a bare SCC
cartridge with no ROM window at all, mapped on request when you leave the menu
with `scc_on_exit=1`, or by the preloader as a fallback when `loader.rom` is
missing. SCC I/O stays live while the MSX BIOS boots on to disk or BASIC.

ROM headers marked `ASCII16X`, `ROM_NEO8` or `ROM_NE16` are recognised by the
detector but **not** emulated — such a ROM fails to mount and the menu stays on
screen.

If a ROM is 128 KB or smaller it is loaded into RAM cache. Larger ROMs are
written to the 1 MB permanent flash region and read directly from flash.

### Forcing a mapper by filename

If automatic detection picks the wrong mapper, you can force one by adding a
tag to the filename **before** the `.rom` extension:

| Tag | Mapper |
|-----|--------|
| `[Plain]` | Plain / linear ROM |
| `[Konami]` | Konami (8 KB pages) |
| `[KonamiSCC]` | Konami SCC |
| `[ASCII8]` | ASCII8 (4 × 8 KB pages) |
| `[ASCII16]` | ASCII16 (2 × 16 KB pages) |

Example `loader.ini` entries:

```ini
fav0=game[Plain].rom
fav1=puzzle[ASCII8].rom,Puzzle Game
fav2=biggame[ASCII16].rom
fav3=gradius[KonamiSCC].rom,Gradius 2
fav4=nemesis[Konami].rom
```

If no recognised tag is present, the firmware falls back to automatic mapper
detection. Tags are matched as substrings, so the exact placement inside the
name does not matter as long as it appears before `.rom`.

---

## API documentation

The Z80↔Pico command protocol used by the boot menu is documented in
[`docs/omniscc_api.md`](docs/omniscc_api.md). It covers the memory map,
command/status register at `0x7FFF`, the `0x6000-0x7FFE` exchange buffer, and
the file-system/dir commands available to MSX-side code.

---

## Useful commands

```bash
# Release build
cmake -S . -B generated/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build generated/build

# Flash with OpenOCD
cmake --build generated/build --target flash

# Inspect sizes
arm-none-eabi-size generated/build/OmniSCC.elf

# Check that hot-loop functions are in RAM
arm-none-eabi-nm -C generated/build/OmniSCC.elf | grep Execute

# Guess a ROM's mapper type without hardware
python3 test/detect.py <rom>    # mirrors COmniSCC::DetectCartType()
python3 test/romid.py <rom>     # dumps every LD (nn),A with disassembly
```

---

## Troubleshooting

- **Menu does not appear / MSX crashes:** Make sure you are using a Release
  build. Debug builds cannot meet the Z80 bus timing.
- **USB drive does not appear:** The cart must be powered from USB **without**
  the MSX driving `/SLTSL`. If the MSX is on, the cart stays in cartridge mode.
- **`loader.ini` changes are lost:** Edit the file on the USB drive and safely
  eject before rebooting. The browser rewrites the whole file when you boot an
  entry; the favourites menu changes only `default=` when you manually boot
  a ROM.
