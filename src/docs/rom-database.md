# ROM mapper database

Copy `romdb.bin` from the release's MSX tools to the cartridge volume root,
alongside the matching `loader.rom` / `loader_fav.rom`. The database is an
ordinary file; updating it does not require flashing firmware.

Both menus accept this setting in their respective `loader.ini` / `loader_fav.ini`:

```ini
romdb=/romdb.bin
```

Use another path (up to 64 characters) to select another database, or `romdb=`
to disable hash lookup. An omitted key selects `/romdb.bin`. Overlong values
are ignored; paths cannot contain a line break. Leading spaces after `=` are
skipped; trailing spaces and inline comments are part of the path. Favourites
preserves the setting when saving; the browser includes it in its INI rewrite.
API clients can set the same path with loader command `08h` before mapping a ROM.

Selection order:

1. A recognized `[Mapper]` in the ROM's basename wins, ignoring letter case.
   When multiple recognized tags exist, the first tag wins.
2. Otherwise, a valid database is searched using the first seven bytes of the
   ROM's full SHA-1. A match selects the mapper and attempts to rename, for
   example `Game.rom` to `Game[KonamiSCC].rom`.
3. If the file is missing, disabled, invalid, or has no matching hash, the
   existing signature/instruction heuristic runs. The menu itself is recognized
   by its `OmniSCC` signature and is never hash-tagged.

Both menus hide recognized mapper tags in graphic and MSX1 text modes. Other
bracketed text, such as `[English]`, remains visible. Actual on-disk filenames
retain the tags. Tags never grant support for an unimplemented mapper: a known
unsupported type reports `UNSUPPORTED ROM MAPPER` (`2Ah`), rather than silently
choosing an incompatible mapper. Current supported families are Plain (including
Normal/Mirrored with header-based placement or explicit `0x0000`/`0x4000`/`0x8000`
and `Mirrored4000` placement), Generic `8kB`, Konami, KonamiSCC, ASCII8,
ASCII16, SCC-I/SCCPlus and KUC, with their size/address-decoding limits. The complete
upstream type catalogue is retained for future mapper implementations.

Hashing and rename run before final flash address mapping and before KUC opens
its `.sav`; a newly created save therefore uses the final ROM basename. Existing
save migration is not provided. A failed rename does not prevent selection of
the detected mapper. Existing destinations are never overwritten. Automatic
renames are skipped if the resulting basename exceeds the browser's 31-character
complete-name field; manually tagged long files remain supported as before.
Missing old `last=` / favourite paths resolve to a unique tagged successor in
the same directory. Ambiguous matches report a missing ROM instead of guessing.

The database contains **9681 unique hashes** and 65 mapper names.
Its total size is **79600 bytes** (about 77.7 KiB), including metadata and names. No seven-byte
prefix collisions occur in these inputs. Hashing is done at mount time only;
search needs at most 14 comparisons for this database. Validation streams the
file once, then binary search reads individual records; the full database is
never allocated in cartridge RAM. Renamed files bypass subsequent hash lookup.
These bounds are structural; mount latency on RP2040 has not been measured.

## Recreate or update

From `src/`:

```sh
python3 tools/build_mapper_database.py
```

Outputs are `generated/romdb.bin`, `generated/mapper_database.hxx` and
`generated/mapper_tags.inc`. Normal CMake builds regenerate these offline when
the script or inputs change. Only names/tag recognition are compiled into the
firmware and menus; hash records remain in the external binary.

The pinned upstream input is `tools/mapperdb/softwaredb.xml`; its provenance is
in `tools/mapperdb/README.md`. Replace that snapshot to update upstream data.
Local additions belong in `tools/mapperdb/custom_roms.txt`:

```text
# Full SHA-1 of the unmodified ROM, then mapper name; comments are allowed.
# Example only: replace this hash with the SHA-1 of your ROM.
0123456789abcdef0123456789abcdef01234567 KUC
```

To correct an existing upstream entry, append the third field `override` to its
custom line: `FULL_SHA1 SCCPlus override`. This is allowed only for a hash already
present upstream. Ordinary custom entries cannot replace an upstream type;
conflicting custom entries for one hash are always rejected.

The script accepts `--xml PATH --custom PATH --output DIRECTORY`. Except for an
explicit upstream override, it rejects conflicting types for the same full hash
and **any** collision between different
full hashes after truncation to seven bytes, even if their mapper is identical.
Identical full-hash/type duplicates are deduplicated. Rows without a mapper are
skipped and counted. Mapper names must be 1–31 ASCII letters/digits/`_`/`-`.
A new mapper name also requires rebuilding firmware/menus to recognize that
name; existing names can receive new hashes through a database-only update.

## Binary format, version 1

All integers are unsigned little-endian. The 40-byte header contains:

| Offset | Field |
| --- | --- |
| 0 | Eight-byte magic `OSROMDB1` |
| 8 | Format version, 1 |
| 12 | Record stride, 8 |
| 16 | Record count |
| 20 | Dictionary slot count, including empty ID 0 |
| 24 | Record offset (`40 + dictionary_count * 32`) |
| 28 | Total file size |
| 32 | Standard CRC32 of dictionary + records |
| 36 | Standard CRC32 of header bytes 0–35 |

Dictionary slots are 32-byte NUL-padded mapper names. ID 0 is reserved/empty.
Records are sorted lexicographically by their seven SHA-1 bytes; byte 7 is the
nonzero dictionary ID. Records begin on an eight-byte boundary. IDs are local
to each file, so adding an existing mapper's hashes does not require matching
ID numbering in the firmware. The reader checks CRCs, lengths, dictionary
names, IDs and strictly increasing prefixes before using the database.
