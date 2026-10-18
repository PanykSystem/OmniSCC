# Standard system files for OmniSCC.dsk

`OmniSCC.dsk` is a bootable 720 KiB FAT12 image labelled `OMNISCC`.
It contains the standard **MSXDOS.SYS 1.03** and **COMMAND.COM 1.11**
(Tim Paterson version with the file-concatenation fix), all seven OmniSCC
COM utilities, `README.TXT` and `LICENSE.TXT`. It requires 64 KiB RAM.
These DOS1 system files work on MSX1 and later machines with a disk interface.
The old nonstandard `COMMAND.SYS` has been removed.

The system files and MSX boot sector are copied unchanged from the
[official Nextor 2.1.0 tools disk](https://github.com/Konamiman/Nextor/releases/tag/v2.1.0)
(`tools.dsk.zip`, actually a 7z archive containing `nextor.dsk`).
They are the classic DOS1 files, not the Nextor kernel or COMMAND2/3.
The included 1.11 interpreter is the latest standard MSX-DOS 1 command
interpreter; newer COMMAND2/3 versions need their corresponding DOS kernels.
See [MSX-DOS/Nextor licensing terms](NEXTOR-LICENSE.md).

SHA-256 of the original system files:

```text
f65e3ac22f0c8eb842e1863fa885aeb8cef4e0ace02efff92e2bb311db2de469  MSXDOS.SYS
7600d3ccfdac18a11b59b8d91b43120d8b1d36b4354e503cdb9a3f9fd88e6293  COMMAND.COM
```

SHA-1 of COMMAND.COM: `af5f2ef3eac4062638f5d8069515e6bcee788e7d`.
The separate system files match the copies inside `OmniSCC.dsk` byte for byte.

The firmware uses the same command names in BASIC and DOS:
`OMOUNT`, `OEJECT`, `ODISK`, `ODIR`, `ODEL`, `OCOPY`, `ONEW`.
BASIC prefixes them with `CALL`; DOS uses the matching `.COM` files.
The previous BASIC MOUNT/EJECT/DISK/DIR names and DOS MOUNT/EJECT/DISK
executables have been replaced. The cartridge API command numbers are unchanged.
