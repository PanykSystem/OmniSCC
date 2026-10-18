# OmniSCC Firmware

> **Upgrade warning — firmware 2.0.0:** This firmware changes the flash layout.
> On first boot after upgrading from 1.0.0, the cartridge formats the old-layout
> volume and **all existing files are lost**. Back up ROMs, disk images, loader files and
> settings before updating if you need to keep them; copy the loader and other
> files back afterwards. Subsequent boots preserve the new volume.


Firmware for the OmniSCC — an RP2040-based MSX multi-cartridge that loads ROM
and `.dsk` floppy images from the Pico's external 16 MB flash. In an MSX it
emulates plain and mapped ROM cartridges, the Konami SCC sound chip, and floppy
drives backed by disk images. The browser menu lets you select a ROM or mount a
disk image; an alternative menu offers ten configurable favourites.

**ROM-game saves:** KUC supports flash saves in a companion `.sav`
for ROMs explicitly named with `[KUC]`, on compatible 16 MiB SPI NOR hardware.
See [KUC setup, capacity and validation status](docs/kuc.md).
For an unknown flash profile, both menus accept `kuc_allow_unknown_flash=1`
(default `0`); the hardware requirements still apply. Other cartridge flash,
SRAM and EEPROM profiles remain unsupported. Writable `.dsk` games continue to
save into their disk image.

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
`msx/loader_fav.asm` → `generated/loader_fav.rom` (ten favourites, 16 KiB).
The favourites menu selects SCREEN 1 on MSX1 or unknown BIOS versions, and SCREEN 5
on recognized MSX2+ BIOS versions after a restoring probe confirms 32 KiB VRAM. Copy
**one** of them to the root of the cartridge's USB drive under its original name after
the first boot (see below).

Both graphical menus show a centered OmniSCC logo while reading configuration
and preparing the menu. With at least 64 KiB VRAM, initialization uses a hidden
page and reveals the completed menu without a partial redraw. The splash adds
no fixed delay; MSX1 text mode and 32 KiB graphics retain the hidden-screen startup.

The browser uses a native bitmap title and the ASCII half of the shared font.
After changing the artwork in `msx/build_loader_art.py` or the shared `msx/font.inc`,
run `python3 msx/build_loader_art.py` from this directory to refresh the checked-in
`loader_title.inc`, `loader_title.png` preview and `loader_font.inc`.
The same generator creates `loader_fav_title.inc` and `loader_fav_title.png`
for the favourites menu's OmniSCC launcher header; favourites retain the full font.

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
2. A removable drive named **OmniSCC** appears.
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
menu ROMs, plus a sample `loader_fav.ini.example` for the favourites menu. Copy the
chosen menu ROM onto the cartridge under a name matching `loader*.rom`. Copy
`loader_fav.ini.example` to the cartridge as `loader_fav.ini`, then edit the
favourites to match your files.

> **Tip:** The firmware uses FatFs with long-filename and exFAT support, but
> keeping filenames short and without spaces avoids any potential issues with
> the simple Z80-side file parser.

---

## Multi-disk games

Keep all disks of a game in one directory. Downloaded names with a
`(Disk x of y)` marker are recognized automatically, with `1 <= x <= y <= 9`:

```text
Games/Game (Disk 1 of 2).dsk
Games/Game (Disk 2 of 2).dsk
```

The marker may appear anywhere in the basename, including before other dump
tags: `Game (1988)(Publisher)(Disk 1 of 2)[a].dsk`. Apart from the disk number,
the names must match, including the total and any tags after the marker.
The earlier `Game.disk1.dsk`, `Game.disk2.dsk` naming rule also works (1–9).

Select the first disk in the existing loader. The firmware discovers the set
automatically; selecting another member starts with that disk instead. The
loader still lists each image separately and needs no update. The suffix and
extension are case-insensitive. Ordinary names such as `Ys2.dsk` remain single
images. Long FAT filenames work through their actual 8.3 aliases, including
names longer than the mailbox's 64-character path limit. Each path used for
access (the directory plus a full name or its alias) must fit that limit.

When the game requests a disk, hold exactly one top-row digit **1–9** and
confirm with the game's own disk-check key (Space, Enter, etc.). Enter, Ctrl
and Shift are not required by OmniSCC. **Drive A** changes immediately before
the next `DSKIO` or `DSKCHG` call. Hold the digit until that access; there is
no background keyboard hook or on-screen menu. Zero and multiple digits are
ignored. Drive B keeps its independently selected image.

Set `multidisk=0` in the active menu INI (`loader.ini` or `loader_fav.ini`) to
disable numbered switching (default: `multidisk=1`). This avoids collisions
with game controls. Manual `CALL OMOUNT` and drive B continue to work.

Selecting the inserted disk does nothing. Missing or invalid members, failed
opens and failed synchronization leave the current disk in place. Before a
successful replacement the firmware persists pending writes and signals a
media change once. The set is discovered at mount time; switching does not
rediscover it. Images added later require an explicit remount.

This works with games using the disk BIOS. Loaders accessing FDC registers
directly require controller emulation, which this firmware does not provide.

If games fail or glitch with other Disk ROMs and drives installed, try
[boot-key options to free RAM](#disk-games-fail-when-other-disk-interfaces-are-present).

## Disk commands from MSX-DOS

The firmware gives all cartridge disk commands the `O` prefix in both
BASIC and DOS: `OMOUNT`, `OEJECT`, `ODISK`, `ODIR`, `ODEL`, `OCOPY`, `ONEW`.
The old names are replaced. [`../tools/OmniSCC.dsk`](../tools/OmniSCC.dsk) now includes all seven
utilities and standard MSX-DOS 1.03 / COMMAND 1.11 for machines with 64 KiB RAM.

`OMOUNT.COM`, `OEJECT.COM`, `ODISK.COM`, `ODIR.COM`, `ODEL.COM`, `OCOPY.COM` and
`ONEW.COM` expose the BASIC extensions as DOS commands through the same
cartridge API. Each has its own
assembly source in [`msx/dos/`](msx/dos/README.md). They build with the firmware
into `generated/build/dos/`, or independently without the Pico SDK:

```sh
cmake -S msx/dos -B generated/dos -G Ninja
cmake --build generated/dos
```

Put the COM files inside a DOS disk image, then use e.g. `OMOUNT B:GAME.DSK`,
`OEJECT B:`, `ODISK`, `ODIR GAMES`, `ODEL OLD.DSK`,
`OCOPY SOURCE.DSK BACKUP.DSK` or `ONEW EMPTY.DSK`. Paths name files on the
cartridge's outer volume. `ONEW` defaults to a 720 KiB FAT12 image and accepts
`360` for a 360 KiB image. `ODEL` refuses mounted images; `OCOPY` and `ONEW`
never overwrite existing files. See the [command guide](msx/dos/README.md) for
quoted paths, limits, slot discovery, media changes, and verification.

## Disk commands from BASIC

Once a `.dsk` image is running, four cartridge-published statements —
`CALL OMOUNT`, `CALL OEJECT`, `CALL ODISK` and `CALL ODIR` — change or show what is
mounted without a reboot. They work the same way `CALL FORMAT` already does:
nothing to load, they are just there at the Disk BASIC prompt.

```basic
CALL OMOUNT("GAME2.DSK")        ' replaces what is in drive A
CALL OMOUNT("B:SIDE-B.DSK")     ' replaces what is in drive B
CALL OEJECT("B:")               ' empties drive B; no argument means A
CALL ODISK                      ' what is mounted in A and B
CALL ODIR                       ' the cartridge volume's root
CALL ODIR("GAMES")              ' a subdirectory of it
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

`CALL ODISK` prints one line per drive:

```
A: GAME2.DSK  1440K
B: -- empty --
```

with `  (protected)` appended when the mounted image is read-only.

`CALL ODIR` lists the cartridge's own FAT volume, not either drive — the
mounted images are files on that volume, and this is how you find one to
mount. It shows only what `CALL OMOUNT` can open: directories and `.dsk`
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
listed by its 8.3 alias (`METALG~2.DSK` above), which `CALL OMOUNT` opens just
the same. A line is 38 columns at most, sized for `SCREEN 0`'s 40-column line
(a long name wraps under `SCREEN 1`'s narrower one). The listing pauses every
22 lines, the path line included, for any keypress, with no prompt printed and
no way to cancel it early.

**The `B:` prefix.** `CALL OMOUNT` and `CALL OEJECT` take an optional `A:` or
`B:` in front of the path; leaving it off means drive A. Any other letter is
refused on the spot with `NO SUCH DRIVE`, before anything reaches the
cartridge. `CALL ODIR` goes through the same parser, so it accepts an `A:` or
`B:` prefix too and then ignores it — the volume it lists has no drives of its
own, so `CALL ODIR("B:GAMES")` lists `GAMES` just as `CALL ODIR("GAMES")` does.

**The argument is a literal, not an expression.** `CALL OMOUNT(F$)` or
`CALL OMOUNT("B:" + D$)` is a syntax error: evaluating a variable would mean
running BASIC's own expression evaluator while this code is still executing
out of the cartridge's page 1, which needs `CALBAS` and that switches page 1
out from under it. Use `CALL ODIR` to find the name, then type it as a literal.

**Changing a disk with a file open on it loses whatever was buffered** —
exactly like pulling a real floppy out while Disk BASIC still has one open.
`CALL OMOUNT` and `CALL OEJECT` do not check for an open file first; that is
stated here rather than prevented. A `CALL OMOUNT` that fails — a name with no
file behind it, a directory, or a file that is not a whole number of 512-byte
sectors — leaves the drive's current disk in place and prints
`FILE NOT FOUND`; a typo does not eject the running disk.

These four statements are implemented and pass the project's host-side test
suite. The bus pattern the firmware relies on to open the handler's window —
BASIC's `CALL` dispatch reading `0x4004` then `0x4005` and entering the vector
on the very next cartridge cycle — has been measured in openMSX on both
supported Disk ROM dumps, and the sniffer matches it. All four statements have since been run
end to end in openMSX (`CALL ODISK`, `CALL ODIR` in the root and a subdirectory,
`CALL OMOUNT`/`CALL OEJECT` for drive B, `CALL FORMAT` chaining to the vendor,
`CALL FOO` refused with `Syntax error`), with the interrupt-hook restore
confirmed working on every exit. On 2026-09-15 the user reported the four
statements working on real hardware, with `SCCMUZAX.DSK` and the other disk
images booting against the 301-byte reservation — reported working, not
verified item by item; the per-statement hardware checklist (`CALL ODIR`
within seconds of a disk access, ten `CALL ODISK`s in a row) was not run
individually.

Full command codes, buffer layouts and statuses are in
[`docs/omniscc_api.md`](docs/omniscc_api.md).

---

## Which menu ROM to copy

Copy one menu ROM to the volume root. The preloader selects the first regular
file matching `loader*.rom`, ignoring case, in directory order (not alphabetical
order). `loader.rom` has no special priority, so keep only the intended loader
in the root. Hidden/system entries and subdirectories are skipped.

Custom loaders may keep a mapper tag in their filename, for example
`loader[ASCII8].rom` or `loader_custom[KonamiSCC].rom`. The full name is passed
to the normal ROM mounting logic, which applies the existing case-sensitive
mapper tags. If no loader matches or the first match cannot be mounted, boot
falls back to `/nextor.rom`, then bare SCC.

| Built file | Menu |
|---|---|
| `generated/loader.rom` | A browser over the volume: subdirectories, scrolling, and the last choice remembered. Already the right name — copy it as it is. |
| `generated/loader_fav.rom` | Ten favourites listed in `loader_fav.ini`. Simple, predictable, nothing to browse. Copy it under its original name. |

The browser reads and saves `/loader.ini`; favourites reads and saves
`/loader_fav.ini`. Keep both configuration files when switching menus; no INI
rename is needed. Keep only one `loader*.rom` on the cartridge. The sections
below cover the browser first, then the favourites menu.

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
| `F1` | Show Help; `ESC` closes it and restores the current selection |
| Cursor left / right | Page up / page down |
| `RETURN` / `SPACE` | Enter a directory, or boot a file |
| `BS` | Up one directory — or select the `..` row, which every directory but the root lists first |
| `INS` | Assign the highlighted `.dsk` to **drive B**; repeated INS keeps it assigned. A diskette icon marks the assigned image; the bottom line and `disk2=` are updated immediately |
| `DEL` | Clear drive B regardless of the selection; update the bottom line and `disk2=` immediately |
| `ESC` | Return to BASIC (mapping the bare SCC first if `scc_on_exit`) |

The seconds left before the saved entry boots by itself appear in a fixed
24×12 white badge with rounded corners to the right of the logo. Red digits
use the original 8×8 font; one digit and two-digit values are centered in the same
badge. Any key stops the countdown for good and clears the complete badge.
The centered footer shows F1 Help and Return Open with a cyan graphic star between them.
After INS/DEL, drive B status replaces the hint until the next keyboard or
joystick input. The hint then returns and the input performs its normal action.
Only folders and the assigned B: image have icons; ordinary ROM/DSK files do not.
Help shows key names in yellow and actions in white.

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

### SCC volume

The firmware and menus support a global SCC master level in
`loader.ini` (browser) or `loader_fav.ini` (favourites):

```ini
scc_volume=100
```

Use a decimal percentage from `0` to `100`. The default is `100`, which keeps
the previous output level; `50` halves the signal amplitude and `0` mutes SCC.
This is a linear amplitude control, so half the value need not sound half as
loud. The MSX's built-in PSG is unaffected. Channel volumes programmed by the
game keep their relative levels.

The setting applies to SCC ROM games, disk/Nextor SCC audio and the bare SCC
mode selected on exit. It survives SCC chip resets and cartridge changes.
Each menu reads its own INI at startup; restart the menu after editing it.
Invalid or out-of-range values leave the previous value (initially `100`)
unchanged. The browser preserves the setting when rewriting `loader.ini`;
the favourites menu preserves the other lines when saving volume and `default=`.
Install the matching UF2 and updated menu ROM together to use this option.

In either menu, press **`+` / `-`** to change the level in **10% steps**,
clamped to 0–100%. A centred `SCC Volume: 100%` popup appears only while
adjusting, then disappears after about two seconds. ESC dismisses the popup;
press ESC again to leave the menu. Other navigation keys dismiss it and work
normally. Any adjustment cancels auto-boot. The setting is saved when you boot
an entry or leave the menu (and whenever the browser otherwise saves its INI),
not on every keypress. In favourites, literal `-` now controls volume instead
of acting as the AZERTY shortcut for slot 6; the digit `6` still selects it.

### `loader.ini` reference (browser menu)

The browser owns the file and **rewrites it** every time you boot an entry: its
settings and your `name=` lines, in the order you wrote them. Comments and any other
key are lost on the first boot, which is the price of not needing to edit the
file at all.

```ini
last=/games/mg[KonamiSCC].rom
disk2=/games/side-b.dsk
delay=500
diskrom=/roms/yis805-128r2_disk.rom
scc_on_exit=1
scc_volume=100
color_dir=15
color_rom=10
color_dsk=7
name=mg[KonamiSCC].rom,Metal Gear
name=sd-snatcher[SCC].rom,SD Snatcher
```

| Key | Value | Description |
|-----|-------|-------------|
| `last` | path | The entry booted last. Sets the auto-boot target and the directory the browser opens in. A `.dsk` here mounts as a disk; anything else boots as a ROM. |
| `disk2` | path | Image for drive B, mounted alongside `last` whenever that boots a `.dsk`. Set it with `INS` in the browser or by hand; `DEL` clears it. An image that will not open leaves drive B empty rather than failing the boot. |
| `delay` | milliseconds | Countdown before `last` boots automatically. Any keypress cancels it; `delay=0` disables auto-boot. |
| `diskrom` | path | Disk ROM dump to patch for `.dsk` entries, instead of `disk.rom`. A path, so the dump can live in a folder — `diskrom=/roms/yis805-128r2_disk.rom` — at most 64 characters, and no spaces in it. |
| `multidisk` | `0` or `1` | Enable numbered disk switching (default `1`). Set `0` to disable it. |
| `scc_on_exit` | `0`/non-zero | If non-zero, `ESC` maps the bare SCC cartridge. |
| `kuc_allow_unknown_flash` | `0` or `1` | Permit an unknown compatible 16 MiB SPI NOR profile for KUC with `1`; default `0`. See [flash requirements](docs/kuc.md#physical-flash-and-timing). |
| `scc_volume` | `0..100` | Global linear SCC amplitude in percent, default `100`; `0` mutes. Does not change the MSX PSG. Invalid values leave the previous/default value unchanged. |
| `color_dir` | `0..15` | MSX palette index for directory names. Default: `15` (white). |
| `color_rom` | `0..15` | MSX palette index for ROM names. Default: `10` (yellow). |
| `color_dsk` | `0..15` | MSX palette index for disk image names. Default: `7` (cyan). |
| `name` | `<filename>,<title>` | Lists that file under `<title>` instead of its filename. Repeat the key once per file; the browser keeps these lines when it rewrites the file. |

Color values are decimal MSX palette indices. Empty values and values outside
`0..15` leave the current setting unchanged. The browser preserves the configured
colors when it saves `loader.ini`; the background, frame, Help and logo use fixed colors.

`name=` is matched on the **filename alone**, with no path, so the same title
applies to that filename in any directory — which is what makes the lines
writable by hand: you see a name in the browser and write that. Case does not
matter, spaces around the `=` and the comma are ignored, and a title may contain
spaces and commas. Directories can be titled too. The file is still opened by its
real name, so a mapper tag in it keeps working.

A missing or unreadable `loader.ini` is not an error: the browser opens at the root with
a 500 ms delay and nothing to auto-boot. If `last` names a file that has since
been deleted, the mount fails with a message and the countdown is not armed
again, so it cannot loop.

#### Mount errors (both menus)

Failed manual launches and auto-starts show the specific reason and the
problem file's path, followed by `PRESS ANY KEY TO RETURN TO MENU`.
Reasons distinguish missing ROM/DSK files, invalid image sizes, missing or
wrong-size Disk ROM dumps, unsupported Disk ROMs, patch failures, read errors
and cartridge response timeouts. For dump errors the path is `diskrom=` or
`disk.rom`, rather than the game image. Unknown errors retain a fallback message.
After acknowledgement the menu returns with auto-boot cancelled, even at
`delay=0`; a missing optional drive-B image still leaves B empty.
A cartridge response timeout instead shows `PLEASE RESTART THE MSX` and waits
for restart: the display runs from RAM because the loader ROM is unavailable.

Detailed reasons require the updated UF2 as well as the menu ROM. Older
firmware only reports a generic failure. See the
[mount error reference](docs/omniscc_api.md#mount-failure-codes-0x01-and-0x03)
for exact messages and status codes.

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
`generated/loader_fav.rom` to the volume root to use it, and write the
ini below. Both menus share the color and boot options, while favourites use
`favN=` and `default=` instead of browser paths and labels.

The 16 KiB favourites menu automatically uses SCREEN 1 on MSX1, including MSX1
with V9938, and SCREEN 5 on recognized MSX2+ BIOS with at least 32 KiB VRAM.
SCREEN 1 retains ten slots, labels, digit shortcuts, joystick, Help and auto-boot;
it uses the shared 8×8 font, rounded frame and inverse selection.
Configured file colors apply in SCREEN 5; SCREEN 1 uses fixed tile colors.
The centered Help/Open hint matches the browser in both modes.
Screenshots: [MSX1](../images/loader-fav-msx1-screen1.png) and
[MSX2](../images/loader-fav-msx2-screen5.png).

### `loader_fav.ini` reference (favourites menu)

`/loader_fav.ini` tells the boot menu which ROMs to show and which slot to start.
Create it from the supplied `loader_fav.ini.example` and adjust the entries.
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
scc_volume=100

; decimal MSX palette indices for ROM and disk text (0..15)
color_rom=10
color_dsk=7
```

### Keys

| Key | Value | Description |
|-----|-------|-------------|
| `favN` | `filename.rom[,label]` or `filename.dsk[,label]` | Assign a ROM or `.dsk` disk image to menu slot `N` (0–9). A name ending in `.DSK` (case-insensitive) is mounted as a disk image; anything else boots as a ROM. The optional `label` is shown in the menu. |
| `default` | `0..9` | Slot that is highlighted when the menu starts. |
| `delay` | milliseconds | Countdown before the highlighted slot boots automatically. Defaults to 500 ms; `delay=0` disables auto-boot. |
| `diskrom` | `path` | Disk ROM dump to patch when a `.dsk` entry is booted, instead of `disk.rom`. A path, so the dump can live in a folder — `diskrom=/roms/yis805-128r2_disk.rom` — at most 64 characters and no spaces. Applies to every `.dsk` entry in the file. |
| `multidisk` | `0` or `1` | Enable numbered disk switching (default `1`). Set `0` to disable it. |
| `scc_on_exit` | `0..9` | If non-zero, pressing `ESC` in the menu maps the "SCC only" cartridge. |
| `kuc_allow_unknown_flash` | `0` or `1` | Permit an unknown compatible 16 MiB SPI NOR profile for KUC with `1`; default `0`. See [flash requirements](docs/kuc.md#physical-flash-and-timing). |
| `scc_volume` | `0..100` | Global linear SCC amplitude in percent, default `100`; `0` mutes. Does not change the MSX PSG. Invalid values leave the previous/default value unchanged. |
| `color_rom` | `0..15` | Text color of ROM favourites, including their slot numbers. Default: `10` (yellow), as in explorer. |
| `color_dsk` | `0..15` | Text color of `.dsk` favourites, including their slot numbers. Default: `7` (cyan), as in explorer. |

Colors are classified from the actual filename, case-insensitively for `.dsk`,
regardless of the optional display label. The selected row swaps its file color
with the fixed blue background (`4`). Frame, Help and bitmap logo colors are fixed.
Use decimal values `0..15`; whitespace after `=` and trailing `;`/`#` comments
are allowed. Empty, nonnumeric or out-of-range values leave the previous setting
unchanged. `color_dir` applies only to the browser; the launcher has no directories.
Saving the default slot preserves the color lines and comments exactly as written.

The countdown uses VBlank frames at ~50 Hz. Positive delays shorter than one
frame use one frame; `delay=0` disables auto-boot. Any keypress cancels an
enabled countdown permanently for that session.

### Navigating the menu

| Key | Action |
|-----|--------|
| Cursor up / down | Move the highlight; empty slots are skipped |
| `F1` | Show Help; `ESC` closes it and restores the selected slot |
| `RETURN` / `SPACE` | Boot the highlighted slot |
| `+` / `-` | Change SCC volume by 10%; show a temporary centred popup. ESC closes the popup. |
| `0`–`9` | Boot that slot directly |
| Shifted digit symbols | Boot the corresponding slot directly |
| `ESC` | Return to BASIC (mapping the bare SCC cartridge first if `scc_on_exit` is non-zero) |

### Auto-saving the default slot

When you manually select a slot and boot a ROM, the loader rewrites
`/loader_fav.ini` so the selected slot becomes the new `default=`. Only the single
digit after `default=` is changed; a current `scc_volume=` line is also saved.
Other settings and comments are preserved. The INI must fit in 2047 bytes; an
overfull update is not written, so its volume applies only for this session.
Saving is also skipped if BIOS RAM/stack limits leave no safe temporary buffer.

---

## Supported mappers

The firmware supports the following mapper types:

- Plain / linear ROM (header-based placement, including 48 KiB)
- Generic 8kB (four writable 8 KiB windows; filename or database selection)
- Konami (8 KB pages)
- Konami SCC
- SCC-I / SCCPlus (128 KiB volatile RAM; filename or database selection)
- ASCII8 (4 × 8 KB pages)
- ASCII16 (2 × 16 KB pages)

SCC-I cartridges support the `BFFEh`/`BFFFh` mode register, per-window RAM-write
protection, standard SCC and enhanced SCC+ sound (including an independent fifth
waveform). `[SCCI]` starts in compatible mode; `[SCCPlus]` starts in enhanced
mode for ROM conversions using `B800h`. The ROM image initializes the shared
128 KiB RAM buffer; unused bytes are `FFh`. Writes are volatile and do not create
a `.sav` or modify the ROM file. The instruction heuristic selects neither type.

KonamiUltimateCollection is selected with `[KUC]` or a known hash in the external
[ROM database](docs/rom-database.md), configurable through `romdb=` and API `08h`.
The instruction heuristic does not select KUC.
See [KUC saves and hardware requirements](docs/kuc.md).

A separate mode, **SCC only**, is not detected from a ROM: it is a bare SCC
cartridge with no ROM window at all, mapped on request when you leave the menu
with `scc_on_exit=1`, or by the preloader as a fallback when the selected `loader*.rom` is
missing. SCC I/O stays live while the MSX BIOS boots on to disk or BASIC.

ROM headers marked `ASCII16X`, `ROM_NEO8` or `ROM_NE16` are recognised by the
detector but **not** emulated — such a ROM fails to mount and the menu stays on
screen.

Ordinary ROMs up to 128 KiB are cached in RAM; KUC always uses a verified XIP table. Larger files are read directly from
physical XIP pages through a verified table in `car_cache`; no permanent ROM
copy is created. The table is sized to the file, without a generic 1/2 MiB
storage limit. Mapper addressing limits remain: Plain 64 KiB, Generic 8kB 2 MiB, Konami 256 KiB,
Konami SCC 2 MiB, SCC-I/SCCPlus 128 KiB, ASCII8 2 MiB, ASCII16 4 MiB and explicit KUC 8 MiB. Konami SCC uses an extended
8-bit bank register; SCC enable still tests the low six bits. Unsupported sizes
fail in the menu. ROM execution freezes filesystem ownership until reset.

The FAT region now starts at 1 MiB and spans 15 MiB (30592 logical 512-byte
sectors after the flash translation reserve). New-layout volumes are retained;
recognized old layouts and interrupted formats are recreated automatically.
A damaged flash translation map, including unrecognized nonblank metadata,
is automatically replaced together with a fresh FAT; old files are lost.
If the map is valid but FAT cannot mount, firmware preserves its sectors and
starts USB MSC instead of cartridge mode. Connect by USB, repair the filesystem
or format FAT12/FAT16, safely eject and restart. A durable layout marker in the
reserved firmware area (offset 0xFF000) makes interrupted formatting retry.
If physical flash initialization/formatting fails, USB still enumerates but
MSC remains not-ready; no writes are admitted without a usable raw device.

Hardware timing for direct mapped ROM reads is pending validation on MSX;
Release builds and host tests do not establish physical bus timing.


### Forcing a mapper by filename

If automatic detection picks the wrong mapper, you can force one by adding a
tag to the filename **before** the `.rom` extension:

| Tag | Mapper |
|-----|--------|
| `[Plain]`, `[Mirrored]` | Plain ROM, header-based placement and mirroring |
| `[Normal]` | Plain ROM, header-based placement without mirroring |
| `[0x0000]`, `[0x4000]`, `[0x8000]` | Plain ROM at the specified address, without mirroring |
| `[Mirrored4000]` | Mirrored plain ROM starting at `4000h` |
| `[8kB]` | Generic 8 KiB bank switching, up to 2 MiB |
| `[Konami]` | Konami (8 KB pages) |
| `[KonamiSCC]` | Konami SCC |
| `[SCCI]` | 128 KiB SCC-I RAM cartridge, compatible SCC startup |
| `[SCCPlus]`, `[SCC+]` | Same RAM cartridge, enhanced SCC+ startup |
| `[ASCII8]` | ASCII8 (4 × 8 KB pages) |
| `[ASCII16]` | ASCII16 (2 × 16 KB pages) |
| `[KUC]` | KonamiUltimateCollection, persistent flash (tag or database) |

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

```

---

## Troubleshooting

- **Menu does not appear / MSX crashes:** Make sure you are using a Release
  build. Debug builds cannot meet the Z80 bus timing.
- **USB drive does not appear:** The cart must be powered from USB **without**
  the MSX driving `/SLTSL`. If the MSX is on, the cart stays in cartridge mode.
- **`loader.ini` changes are lost:** Edit the file on the USB drive and safely
  eject before rebooting. The browser rewrites the whole file when you boot an
  entry; the favourites menu preserves other lines while saving the selected
  default slot and SCC volume.

### Older SCC+ ROM conversions hang or play only PSG

Select enhanced SCC+ mode with `[SCCPlus]` or `[SCC+]` in the filename; `[SCCI]`
starts in compatible SCC mode. Some older Konami Game Collection conversions
also contain loader bugs: bank switching with Z80 interrupts enabled, or reliance
on Disk ROM RAM-slot variables before those variables have been initialized.
Such images can work in one emulator configuration and fail on another machine
or boot differently after a reset.

Use a corrected conversion if the mapper is selected correctly but the game
still hangs or its music fails. The
[conversion loader fixes](https://www.msx.org/forum/msx-talk/general-discussion/konami-game-collection-roms-patches)
describe these issues. Changing SCC volume does not repair the game's loader.

### Disk games fail when other disk interfaces are present

Each initialized Disk ROM can reserve MSX RAM for its drives and disk buffers.
If the machine already has another Disk ROM and drives, adding OmniSCC's disk
interface can sharply reduce free RAM. A disk game may then fail to start,
hang or behave incorrectly even though the same image works with fewer disk
interfaces.

On computers with built-in disk drives, try holding **CTRL** immediately after
starting a DSK image in the OmniSCC menu. Keep it held through the restart and
initial disk boot. On Disk ROMs that support this option, CTRL disables the
second logical floppy drive and reduces disk-buffer memory usage, leaving more
RAM for the game. The effect depends on the machine and Disk ROM.

For the supported **Yamaha YIS805 Disk ROM** (`yis805-128r2_disk.rom`),
use the following boot procedure:

1. Select the `.dsk` image in the OmniSCC menu.
2. Press **Enter**, then immediately press and hold **CTRL**.
3. Keep **CTRL held until the game has finished loading**.

Hold CTRL during the restart and disk boot, not just while the menu is visible.
This is a Disk ROM boot option; its behavior can differ with another Disk ROM
or machine.

On some models, holding **DEL** during startup disables the built-in network
BIOS and can free additional RAM for disk games. This is a separate,
machine-specific boot option.

### ROM games fail after a warm reset

If a ROM game does not start from the OmniSCC menu after a reset, switch the
computer off for **at least 10 seconds**, then switch it on and try again.
A warm reset may leave RAM or peripheral state that affects the next launch;
a full power cycle gives the machine and cartridge a fresh startup.

### Disk loaders with fixed RAM addresses

Some disk images install a resident routine at a fixed RAM address instead of
reserving space through DOS. OmniSCC's RAM dispatcher and other disk interfaces
change the available RAM and DOS buffer locations, so such a loader can overwrite
a DOS buffer even when the disk and its files are valid.

### Shared-volume Nextor mode

Select **`nextor.rom`** in either menu to use the cartridge's existing
USB-visible FAT12/16 volume directly as a Nextor disk, without an HDD image.
Build and copy the dedicated `/nextor.rom` and install the DOS system files first.
See [Nextor setup](docs/nextor.md) for build commands, RAM requirements and `nextorrom=` configuration.

Nextor driver/build sources and the pinned upstream snapshot are in [`msx/nextor/`](msx/nextor/). Ready-to-use ROM/DOS files are in [`../tools/`](../tools/).

The firmware keeps mapper hashes in external `/romdb.bin`, configurable through
`romdb=` in either menu INI or API `08h`. Generate it offline with
`python3 tools/build_mapper_database.py`; see [format and custom hashes](docs/rom-database.md).
The linker permits 1020 KiB of firmware while protecting the layout marker at
`0xFF000`; the existing FAT region still starts at 1 MiB.
