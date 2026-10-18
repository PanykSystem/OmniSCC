# ROM mapper database inputs

`softwaredb.xml` is an unmodified openMSX software database snapshot:

- Source: https://github.com/openMSX/openMSX/blob/master/share/softwaredb.xml
- Download: https://raw.githubusercontent.com/openMSX/openMSX/master/share/softwaredb.xml
- Retrieved: 2026-10-18; XML timestamp: `1790115715`.
- SHA256: `7ee9fdb51ff0f046159fe5af5e532fed510b04398ea5e248cf61a7a11e91ec98`.

Its original attribution/copyright block is retained in the XML: Nicolas Beyaert,
BlueMSX Team, openMSX Team and the listed contributors. Database attribution
is not replaced by OmniSCC authorship. No ROM image is included.

Add local ROM hashes to `custom_roms.txt` as `FULL_SHA1 MAPPER`; `#` starts a
comment. Use this file for local additions that are absent from the upstream
database. ROM images are not included.

Run `python3 tools/build_mapper_database.py` from `src/`. See
[`docs/rom-database.md`](../../docs/rom-database.md) for format and update rules.
