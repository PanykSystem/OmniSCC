# OmniSCC Nextor sources

- `nextor_driver.asm` / `nextor_protocol.inc`: the cartridge driver and mailbox.
- `nextor_menu.inc`: the shared kernel-selection menu entry, included by both loader ROMs.
- `build_nextor.py`: assembles our driver and packages the pinned kernel base.
- `upstream/`: the official [Nextor repository](https://github.com/Konamiman/Nextor)
  as a Git submodule pinned to v2.1.3 commit
  `f3e77baa45ca3938f13cc70b57e0a075373ab9f0`. Its source and licence are
  unmodified; update the pin only when deliberately upgrading Nextor.

From the repository root, with Git, Python 3, sjasmplus and a host C compiler:

```sh
git submodule update --init src/msx/nextor/upstream
python3 src/msx/nextor/build_nextor.py
```

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

The official kernel base is `tools/nextor-base.dat`, verified by SHA256.
The submodule revision and pinned MKNEXROM C source are also verified before
compilation. Source checksums normalize CRLF to LF for Windows checkouts.
Outputs go into ignored `src/generated/nextor/`. After submodule initialization,
packaging works offline. This workflow uses the official base rather than
rebuilding all upstream kernel banks with its legacy tools.

Ready-to-use cartridge ROM/DOS binaries are in `tools/`; Pico Release
UF2s are in `firmware/`. See [installation](../../docs/nextor.md).

For a fresh clone, use `git clone --recurse-submodules <OmniSCC repository>`.
An existing clone can initialize all dependencies with
`git submodule update --init --recursive`. Do not use `--remote` for release
builds: the superproject records the tested Nextor commit.
