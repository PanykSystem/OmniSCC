# Nextor and the shared cartridge volume

Nextor mode gives MSX direct access to the existing USB-visible flash volume.
There is no HDD image. Device 1 / LUN 1 uses 512-byte sectors and exactly the
same logical capacity and LBA zero as USB MSC. Keep the volume FAT12 or FAT16.
Legacy single-copy FAT16 volumes with more than 256 sectors per FAT are
exposed through a Nextor-compatible boot sector and FAT address translation.
USB geometry, cluster numbering and root/data addresses remain unchanged.
This view is calculated from the existing BPB; it requires no metadata file.
Nextor writes to its synthetic reserved gap or partition table are rejected
while that compatibility view is active.
Do not repartition or format a volume containing files you want to keep.

## Build the kernel ROM

Our driver and build scripts live in `src/msx/nextor/`. The `upstream/`
subdirectory is a Git submodule of the official Nextor repository, pinned to
v2.1.3 commit `f3e77baa45ca3938f13cc70b57e0a075373ab9f0`. The original source
and licence are unmodified. The official kernel base is
[`tools/nextor-base.dat`](../../tools/nextor-base.dat); the ready-to-use cartridge
ROM is [`tools/nextor.rom`](../../tools/nextor.rom).

The driver uses sjasmplus, Python 3, Git and a host C compiler. Initialize the
submodule once, then packaging works offline without Nestor80 tools. Run from
the repository root:

```sh
git submodule update --init src/msx/nextor/upstream
python3 src/msx/nextor/build_nextor.py
```

For a new clone, use `git clone --recurse-submodules <repository>`. Existing
clones can initialize all dependencies with
`git submodule update --init --recursive`. These commands use the recorded
commit, not the upstream branch's latest version.

On Windows, use a host GCC (for example MinGW-w64) or Clang available on
`PATH`; the Pico ARM cross-compiler cannot build the host packager. The script
checks `cc`, `gcc`, then `clang`, and generates `mknexrom.exe`. To select a
specific executable in PowerShell:

```powershell
$env:CC = "C:/msys64/mingw64/bin/gcc.exe"
python3 src/msx/nextor/build_nextor.py
```

`CC` accepts an executable path, without compiler flags. `SJASMPLUS` may
likewise select the assembler executable. Paths passed to sjasmplus use `/`,
and output directories may contain spaces or be on another drive.

The script verifies the pinned revision, MKNEXROM source checksum and
kernel-base SHA256 before assembling the driver and compiling the packager.
C source checksums normalize CRLF to LF for Windows checkouts.
It writes `src/generated/nextor/nextor.rom`, a build manifest and the licence.
Use `--output PATH` to choose another generated directory; `--upstream PATH`
and `--base PATH` support an external pinned checkout and official base.
The mailbox occupies unused driver-bank bytes, avoiding another permanent
SRAM sector buffer.

The packaged kernel includes one guarded compatibility patch: its boot-sector
classifier accepts the `FAT` prefix, including FatFs's `FAT     ` label as well
as `FAT12`/`FAT16`. Without it the official kernel selects DOS1 for these
otherwise valid existing volumes. The patch changes no filesystem bytes and
is recorded in the manifest; an unexpected kernel layout stops packaging.

## Install

Paths below are relative to the repository root. Ready-to-use UF2 files are
in [`firmware/`](../../firmware/); browser/favourites ROMs, `nextor.rom`,
`NEXTOR.SYS` and `COMMAND2.COM` are in [`tools/`](../../tools/).
Copy the menu ROM and those three Nextor files to the cartridge USB root.
Alternatively copy the generated ROM as `/nextor.rom`. Full Nextor DOS2
mode needs at least 128 KiB of mapped MSX RAM; machines without that mapper
use its DOS1 mode and require suitable DOS1 boot files to obtain a DOS prompt.

Long filenames remain on disk; MSX software accesses their 8.3 aliases.
USB and MSX do not access the volume simultaneously. Before starting Nextor,
the menu closes its files and unmounts FatFs. Nextor writes data and saves the
flash map before acknowledging each completed request. Reset remains in
Nextor mode; power cycle returns to the regular cartridge startup menu.

Select `nextor.rom` in either cartridge menu to start Nextor. The browser
shows the kernel alongside the other ROM files. In the favourites menu, add
an entry such as `fav0=/nextor.rom,Nextor` to `loader_fav.ini`; Enter or its digit
launches it, and a configured default can auto-boot it.

`nextorrom=/path/kernel.rom` identifies a renamed kernel in either menu.
The filename comparison is case-insensitive and uses the last path component;
the selected file's actual full path is passed to Nextor. Paths are limited to
64 characters; an overlong selected path is rejected rather than truncated.
Missing/invalid kernels, unsupported filesystems and storage errors return to
the menu with a message and its countdown stopped.

Without a usable first matching root `loader*.rom`, the embedded preloader tries `/nextor.rom`
automatically. A successful mount resets the MSX so its BIOS discovers the
disk interface. If Nextor is unavailable too, it returns to BASIC with bare
SCC. The preloader uses the fixed root path and does not read `loader.ini`.
F5 no longer starts Nextor. These boot paths do not reformat the shared volume.

## Running DOS programs on the shared FatFs volume

If launching a COM program reports `*** Wrong version of MSX-DOS`, run:

```text
SET EXPERT ON
```

Our shipped COMMAND2 checks whether the source volume has an MSX-DOS2
volume ID before launching COM files, and reports this error for the shared
FatFs format. EXPERT enables COM execution without changing the filesystem.
This is a command-interpreter setting, not a change to the kernel version.

For each boot, add `SET EXPERT ON` to your existing `AUTOEXEC.BAT`. If there
is no AUTOEXEC yet, copy the example from [`tools/AUTOEXEC.BAT`](../../tools/AUTOEXEC.BAT) into
the cartridge root. Keep the other commands in an existing AUTOEXEC.

M 2.6 and the MSXHub MC 1.1.05 package were reproduced failing before this
setting and launching afterwards in actual Nextor/openMSX with 512 KiB
mapper RAM and the legacy 416-sector FAT. Keep each application's companion
files (for example M.OVL) alongside the matching COM file. MC needs at least
256 KiB mapper RAM according to its package requirements.
