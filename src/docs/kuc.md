# KonamiUltimateCollection (KUC)

Select KUC with a `[KUC]` basename tag, for example `Pampas[KUC].rom`, or a
known hash in the external [ROM database](rom-database.md). Tags ignore case;
the first recognized tag overrides the database. The instruction heuristic
does not select KUC. Both menus use the same backend.

KUC provides four 8 KiB windows, bank offsets, four 2 MiB blocks, Konami/Konami
SCC decoding and register locks. It includes SCC/SCC+, A8 decoding restrictions,
unsigned 8-bit DAC and M29W640GB identification/CFI, program/buffer program,
erase, polling, errors and suspend/resume commands. ROMs up to 8 MiB are supported;
unpopulated chip space reads FFh.

## Fixed direct-sector saves

The original ROM remains unchanged. A companion file replaces its extension:

```text
/games/Pampas[KUC].rom
/games/Pampas[KUC].sav
```

The file reserves **128 KiB of sector data**, plus 16 KiB of metadata and a
512-byte header: **147968 bytes (144.5 KiB) total**. It holds 256 saved sectors
of 512 bytes and 512 independent metadata records. Virtual erase records use
metadata only; they do not consume a sector-data slot.

This is fixed storage. The file does not grow during gameplay. The FAT and
physical page maps remain frozen; no runtime file allocation, deletion,
physical erase or filesystem garbage collection occurs.

- First change in a 512-byte sector: copy its complete effective contents,
  apply the mutation, then publish the sector descriptor after durable writes.
- Later programming: write only the changed NOR1→0 bits directly into that
  sector. No per-byte journal record, new sector or FAT update is needed.
- Virtual erase: commit an FFh-range descriptor and update the RAM index.
  Old payload slots become obsolete, but remain occupied until menu compaction.
- Exhausted payload or metadata capacity: report an emulated flash error
  (DQ5), preserving existing mappings/data. Writes to an already allocated
  sector can still clear bits without allocating another slot. Erase requires
  metadata space even when all payload slots are occupied.

Therefore128 KiB is the physical payload reserve, not an unlimited 128 KiB
rewritable disk. Repeated erase/rewrite cycles consume fresh slots until the
next launch. A full 64 KiB save consumes 128 payload sectors; another full rewrite
can consume the remaining 128. Actual consumption depends on which sectors
change. File size alone does not show how much unused capacity remains.

At launch, healthy saves with at least 16 free allocation slots are reused
without rewriting. Near-full saves and interrupted allocation tails are
compacted in the menu. Complete current sectors are preserved, including
unchanged ROM neighbours within each sector. If compaction cannot fit a healthy
save, retain/use the old file; a later allocation may return a flash error.

Before creating a save or its replacement, the firmware checks free FAT clusters.
Insufficient space cancels launch with `NOT ENOUGH SPACE FOR KUC SAVE`; other
preparation errors show `KUC SAVE PREPARATION FAILED`. Press any key to return
to the menu, free space over USB and retry. Existing saves are preserved; a
partial file created by a failed allocation is removed.

Compaction temporarily needs one additional 147968-byte file. `.sav.kuc-new`
and `.sav.kuc-bak` are replacement/rollback names, not permanent backups.
The old file is retained until the replacement is complete and synced. A
missing main file can recover from its rollback file after an interrupted
rename. Ordinary launches do not create a backup or copy the entire file.

## Save association

The file is bound to the ROM's SHA-256 and exact byte size. Rename/move both
files together to preserve that association. Different ROM contents require a
new save; a mismatched file is rejected without replacing it.

## Physical flash and timing

KUC admits these 16 MiB SPI NOR protocol profiles by default:

| Profile | JEDEC ID | Qualification |
| --- | --- | --- |
| BoyaMicro BY25Q128ES | `68 40 18` | Exercised on OmniSCC+ |
| Winbond W25Q128JV-compatible | `EF 40 18` | Protocol checked against the [manufacturer datasheet](https://www.winbond.com/resource-files/w25q128jv%20revg%2004082019%20plus.pdf); MSX timing not yet checked on hardware |

These profiles use 24-bit addressing, 256-byte program pages, WREN `06h`,
Page Program `02h` and WIP bit 0 in status register `05h`. They must support
single-byte/short-span programming and repeated 1→0 programming within the
same page between erases. The board's RP2040 boot/XIP configuration must also
support the fitted chip. Matching a JEDEC profile is not a full capability probe
or a guarantee of identical write latency.

For a different compatible chip, add this to the active `loader.ini` or
`loader_fav.ini` and restart the menu:

```ini
kuc_allow_unknown_flash=1
```

Default `0` allows only the listed profiles. Setting `1` admits other nonblank
JEDEC identities with density byte `18h` (16 MiB); it does not change the SPI
protocol or verify the remaining requirements above. IDs with manufacturer or
memory-type bytes `00h`/`FFh`, and all other density codes, remain rejected even
with opt-in. Flash with a different capacity requires a different firmware/layout.

Programming waits for WIP to clear while the cartridge owns the MSX bus cycle.
A slower chip can increase `/WAIT` time; no universal numerical timing limit
has been established for different MSX machines. Check save/readback and restart
on the actual board when using another chip. Normal mapper and disk operation
do not use this KUC admission setting.

The browser preserves the option when rewriting its INI; favourites retains the
original line. Loader API `09h`, bit 0, controls the same setting before a KUC
mount. The emulator validates API flags but has no physical JEDEC gate.

All writes occur while this cartridge owns a selected bus cycle. The code does
not assert background global `/WAIT` during other-slot/RAM accesses. Ordinary
unchanged ROM reads bypass the timer and command engine.

An existing-sector change fitting one physical page-program span of at most
16 bytes completes synchronously in that selected cycle. Unchanged bytes are
skipped. Larger copies and descriptor publication queue up to 544 bytes and
advance at most one 16-byte program per service call, retaining 600 µs spacing.
The index changes only after the commit is durable. Readback failure reports
an error and prevents further writes through the damaged transaction.

The new backend does not add the old 500 ms virtual erase wait. It preserves
the 50 µs erase-command collection window and stays busy until actual descriptor
persistence completes. Erasing virtual flash does not physically erase the ROM
or the save allocation during play.

The SPI sequence keeps XIP disabled through WREN, program and WIP polling;
XIP is restored only after completion. The bus loop/audio and transitive
service functions execute from SRAM. Games must poll/access the cartridge
until ready; unpolled queued operations need not finish before power loss.

These are software bounds, not a measurement of the full physical `/WAIT`
interval or a guarantee of zero pause. First-sector copies still take longer
than repeated byte programs.

## Validation

Host checks cover all 256 payload sectors after a virtual erase, overflow/DQ5,
repeated writes without allocation, torn metadata/data-only allocation tails,
NOR bit transitions, fixed-capacity physical IO, GPIO loops for both boards,
non-FF ROM neighbours through compaction, actual FatFs and rollback recovery.
The native openMSX adapter shares the format/core and uses the same capacity.

The user confirmed the direct-sector save implementation and the supplied
Pampas variants working on OmniSCC+. Host/native results alone do not
establish physical timing or power-loss behaviour.
