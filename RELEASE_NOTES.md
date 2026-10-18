# OmniSCC 2.0.0 — changes since 1.0.0

## Main changes

- Universal 16 KiB browser and favourites loaders automatically select MSX1
  text mode or MSX2 graphics after checking BIOS and VRAM. Updated interface,
  shared font, joystick support, countdown, F1 Help and detailed mount errors.
  Favourites use their own `loader_fav.ini`; the separate lite loader is retired.
- Nextor accesses the shared USB-visible FAT12/FAT16 volume directly, without
  an HDD image. DOS2 requires at least 128 KiB of mapped RAM. Use `SET EXPERT ON`
  for COM applications; an example AUTOEXEC is included.
- External, updateable `romdb.bin` based on openMSX: 9680 ROM hashes and support
  for local additions. Explicit filename tags take priority,
  followed by the database and existing heuristics. Recognized mapper tags are
  hidden in both menus. Generator, full-hash custom input and collision checks
  are included. Configure the database path with `romdb=` or the loader API.
- KUC flash cartridge support, including SCC+, DAC and persistent companion
  `.sav` files. Each save has 128 KiB of actual sector payload plus metadata
  (147968 bytes total). Repeated byte writes update allocated sectors directly.
  Exhaustion reports a flash error; files do not grow during gameplay.
  Default flash profiles include BY25Q128ES and W25Q128JV-compatible parts.
  Other compatible 16 MiB chips can be enabled with `kuc_allow_unknown_flash=1`
  in either menu INI or loader API `09h`. See [requirements and limits](src/docs/kuc.md).
- SCC master volume: `scc_volume=0..100`, default 100%, or menu `+`/`-` controls
  with 10% steps. Current level appears in Help and a temporary centred popup.
  The setting is saved on launch/exit; MSX PSG levels are unaffected.
- Improved DSK boot memory handling. Multi-disk games switch with one top-row
  digit 1–9 during the game's disk check; confirmation uses the game's own key.
  `multidisk=0` disables this feature. Enter or Space launches the selection;
  browser INS mounts B and DEL ejects B.
- Unified O-prefixed BASIC and DOS commands: OMOUNT, OEJECT, ODISK, ODIR, ODEL,
  OCOPY and ONEW. Updated example disk with standard MSX-DOS system files and
  COMMAND.COM; provenance and licences are included.
- Direct ROM storage: small ROMs are cached in RAM, larger ROMs use verified
  physical flash pages. Improved mapper limits and extended Konami SCC banking.
- Recovery over USB when FAT cannot mount; invalid flash translation metadata
  is recreated together with a fresh filesystem, deleting old files.
- OmniSCC+ PCB revision 1.1 adds a 10 kΩ reset pull-up for USB stability.
  An illustrated modification guide is provided for revision 1.0 boards.
  Updated enclosure models are supplied as 3MF files.

## Installation and upgrade

**Back up all cartridge files before upgrading from 1.0.0.** The flash layout
has changed: the first boot reformats an old-layout volume and deletes its files.

Flash `firmware/OmniSCC.uf2` or `firmware/OmniSCC+.uf2`, then copy
`tools/loader.rom` and `tools/romdb.bin` to the cartridge root. Keep only one
`loader*.rom` there. Restore games, disks and settings after formatting.
For favourites, copy `loader_fav.rom` under its original name and copy
`loader_fav.ini.example` as `loader_fav.ini`, then edit the favourites.
Keep only one `loader*.rom` in the root.
For Nextor, also install `nextor.rom`, `NEXTOR.SYS` and `COMMAND2.COM`.
Safely eject before connecting to the MSX.

Verify the release payload with `sha256sum -c SHA256SUMS`.
