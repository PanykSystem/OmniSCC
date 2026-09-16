#!/usr/bin/env python3
"""Assemble diskstub.asm and derive its relocation table.

The stub is loaded at an address that is not known until run time, so every
absolute address inside it needs fixing up. Rather than maintain that list by
hand, assemble the same source at two origins and diff: any little-endian word
that changed by exactly the origin delta is a relocation site.

Outputs, per part:
    diskstub_<part>.bin   the blob assembled at ORG_A
    diskstub_<part>.rel   uint16 count, then that many uint16 byte offsets
    diskstub_<part>.sym   sjasmplus symbols from the ORG_A pass, so a test can
                          name what it pokes at instead of hunting byte patterns

A .rel offset names a word in the .bin. To load at address `base`, add
`base - ORG_A` to each of those words.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUTPUT = HERE.parent / "generated"
SJASM = os.environ.get("SJASMPLUS") or (
    str(HERE / "sjasmplus.exe") if os.name == "nt" else shutil.which("sjasmplus")
)
if not SJASM:
    raise SystemExit("Install sjasmplus or set SJASMPLUS to its executable path")
SRC = HERE / "diskstub.asm"

ORG_A = 0x8000
ORG_B = 0x9000
PARTS = {0: "rom", 1: "ram", 2: "win", 3: "bas"}

# The parts the firmware patches by addressing "the blob's last two bytes", and
# the cell each of those two bytes is. CRomPatcher::Apply writes the vendor INIT
# address into the ROM part's, CmdDiskSetRam the vendor STATEMENT address into
# the RAM part's.
GUARDED = {"rom": "OrigInitAddr", "ram": "OrigStatementAddr"}

# The parts the firmware serves at a fixed address instead of relocating, and
# the constant that address is named by in diskstub.asm.
FIXED = {"win": "WIN_BASE", "bas": "BAS_BASE"}


def ram_stub_len() -> int:
    """The RAM_STUB_LEN EQU value the ROM part's InitPrologue copies by --
    the count the RAM part must actually assemble to. Read out of the source
    rather than duplicated as a second Python constant."""
    m = re.search(r"^RAM_STUB_LEN\s+EQU\s+(\d+)", SRC.read_text(),
                  re.MULTILINE)
    if not m:
        raise SystemExit("RAM_STUB_LEN EQU not found in diskstub.asm")
    return int(m.group(1))


def assemble(part: int, org: int, out: Path, sym: Path = None) -> bytes:
    # sjasmplus warns ("use / instead") about backslashes in file names, and
    # on native Windows Path.__str__ produces exactly those. Pass POSIX-style
    # paths so that warning does not fire on every single run.
    cmd = [str(SJASM), f"--raw={out.as_posix()}", f"-DSTUB_PART={part}",
           f"-DSTUB_ORG={org}"]
    if sym is not None:
        cmd.append(f"--sym={sym.as_posix()}")
    cmd.append(SRC.as_posix())
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        raise SystemExit(f"sjasmplus failed for part={part} org={org:04X}")
    # A warning here means wrong bytes, not just noise: the two-origin round
    # trip cannot detect a truncated operand, because both origins truncate it
    # the same way. Treat any warning as fatal.
    for line in (res.stdout + res.stderr).splitlines():
        if "warning" in line.lower() and "warnings: 0" not in line.lower():
            sys.stderr.write(line + "\n")
            raise SystemExit(f"sjasmplus warned for part={part} org={org:04X}")
    return out.read_bytes()


def relocations(a: bytes, b: bytes, delta: int) -> list[int]:
    if len(a) != len(b):
        raise SystemExit(
            f"blob length changed between origins: {len(a)} vs {len(b)}; "
            "the source must not contain origin-dependent sizes"
        )
    sites = []
    i = 0
    while i < len(a) - 1:
        wa = a[i] | (a[i + 1] << 8)
        wb = b[i] | (b[i + 1] << 8)
        if wb == ((wa + delta) & 0xFFFF) and wa != wb:
            sites.append(i)
            i += 2          # a relocated word is never half of another
        else:
            i += 1
    return sites


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    tmp = OUTPUT / "_stub_tmp.bin"
    for part, name in PARTS.items():
        blob = assemble(part, ORG_A, OUTPUT / f"diskstub_{name}.bin",
                        OUTPUT / f"diskstub_{name}.sym")

        # Two blobs end with a cell the firmware patches by addressing "the end
        # of the blob": the ROM part's OrigInitAddr, which the INIT tail calls,
        # and the RAM part's OrigStatementAddr, which the BASIC blob chains a
        # foreign statement name through. Appending anything after either one
        # silently moves that patch site, and the MSX then calls an arbitrary
        # address instead. The cell assembles as two zeroes behind a two-byte
        # guard, so the shape is checkable -- and the guard is what makes the
        # check mean something: what precedes it in both parts is data, and
        # could plausibly end in zeroes of its own.
        if name in GUARDED and blob[-4:] != b"\x5a\xa5\x00\x00":
            raise SystemExit(
                f"{name} part must end with the 5A A5 guard and the "
                f"{GUARDED[name]} cell; it ends with {blob[-4:].hex()} -- did "
                f"something get appended after {GUARDED[name]}?"
            )

        # The INIT tail copies exactly RAM_STUB_LEN bytes out of the exchange
        # buffer for the RAM part (see the EQU's comment in diskstub.asm)
        # rather than the full 512-byte reservation. If its real size drifts
        # from that constant, the copy silently truncates or drags stale sector
        # data in behind the dispatcher -- catch it here instead.
        if name == "ram" and len(blob) != ram_stub_len():
            raise SystemExit(
                f"ram part is {len(blob)} bytes but diskstub.asm's "
                f"RAM_STUB_LEN says {ram_stub_len()} -- update the EQU"
            )

        alt = assemble(part, ORG_B, tmp)

        # The two window parts are assembled at their own fixed origins and the
        # firmware serves them there verbatim -- nothing relocates either. So
        # each must come out byte for byte identical whatever STUB_ORG says,
        # and any difference means something in it silently depends on an
        # origin it will never actually be loaded at.
        if name in FIXED and alt != blob:
            raise SystemExit(
                f"{name} part is not origin-independent: building it at "
                f"{ORG_B:04X} instead of {ORG_A:04X} changed its bytes, so it "
                f"contains an address that follows STUB_ORG rather than "
                f"{FIXED[name]}"
            )

        sites = relocations(blob, alt, ORG_B - ORG_A)
        rel = OUTPUT / f"diskstub_{name}.rel"
        rel.write_bytes(
            struct.pack("<H", len(sites))
            + b"".join(struct.pack("<H", s) for s in sites)
        )
        print(f"{name}: {len(blob)} bytes, {len(sites)} relocations")
    tmp.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
