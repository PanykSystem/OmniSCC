# OmniSCC API Specification

## Overview

The OmniSCC API provides a hardware-accelerated interface between the Z80 processor (MSX) and the Raspberry Pi Pico (cartridge). It acts as a synchronous bridge, allowing the Z80 to read/write files, iterate through directories, map ROMs on the fly, and switch to a bare SCC cartridge.

The API is **synchronous from the Z80's point of view**: after writing a command to `0x7FFF`, the Z80 waits for its result. Loader commands and disk commands put the sentinel and status at opposite addresses; see the memory map and “How a disk command replies” below.

1. Poll **both** addresses to distinguish a real reply from an undriven bus. For loader commands, wait for `0x7FFE == 0xA5` and `0x7FFF != 0xA5`, then take `0x7FFF` as the status. For disk commands, reverse the addresses.
2. The poll loop must be fetched from **MSX RAM**, never from this cartridge. Slow handlers serve no cartridge cycles while they run.

The Pico asserts the MSX `/WAIT` signal on *every* cycle in which this cartridge is selected — it goes low as soon as `/SLTSL` drops, before `/RD` or `/WR` has even been decoded — and releases it once the byte has been driven or the write has been stored. Stretching writes as well as reads matters: an SCC driver writes its mode-select register exactly once at init, and losing that single write leaves the SCC invisible for the rest of the session.

For loader commands, a write to `0x7FFF` releases `/WAIT` (and restores core-0 interrupts) before the handler runs. Disk commands have both fast handlers that hold `/WAIT` and slower handlers that release it; see “How a disk command replies” below.

### How a loader command replies

While a handler runs the cartridge answers nothing at all, and this is the part that bites. Two consequences, and the protocol has to survive both.

**The bus floats at a previously driven value,** often the command byte the Z80 itself just wrote. A poll that waits only for "`0x7FFF` is not `0xFF`" reads that byte back and calls the command finished. On hardware that showed up as a menu that loaded one boot in two: `CMD_MAPFILE` is `1`, so the preloader read `1` and reported a mount failure that never happened; when the floating byte was `0x00` it reported success and jumped through a cartridge vector the firmware had not swapped in yet, which hangs. The fix is the sentinel at `0x7FFE`: a floating bus is monochromatic, so requiring two *different* values at two addresses can only be satisfied by the firmware actually driving. No loader status is `0xA5` — they are `0x00`, `0x01`, `0xFE` and FatFs codes `0x00`-`0x13`.

**The poll's own instruction fetches must not come from the cartridge.** `ExecCartCmd` in `msx/mailbox.inc`, which both loaders include rather than copy, is a single `jp` into a copy of itself planted in MSX RAM at `MAILRAM`; the jump is before the command write, not after, because the write is what stops the firmware serving. The stubs in `SAFERAM` carry their own copies for the same reason. A loader.rom that polls from ROM hangs on the first command whose handler outlives the write cycle — which is every command that touches the directory or erases flash.

The status also **repeats** until the next command write, and the firmware marks a command busy *before* it releases `/WAIT`. Repeating means a lost or duplicated bus cycle costs one more poll iteration instead of the result: the read used to consume the status, so one missed cycle left the Z80 polling a register that would never answer again. Marking busy first is what keeps the repeat from handing out the *previous* command's status.

`0x7FFF` still answers the status itself, so a `loader.rom` built before the gate keeps working against a firmware that has it — ungated, with the old failure mode.

The emulator drives the bus in the cycle every read is issued, so neither failure can occur or be reproduced there.

## Memory Map (Cartridge Page 1)

| Address | Name | Access | Description |
| :--- | :--- | :--- | :--- |
| `0x4000 - 0x5FFF` | ROM Window | Read | Maps to the active ROM data. For the loader/preloader this is the embedded stub ROM. |
| `0x6000 - 0x7FFD` | Exchange Buffer | Read / Write | Communication buffer (`CART_BUF`). The top two bytes of the 8 KB window are the two reply registers, so a block payload stops at `0x7FFD` (`LOADER_PAYLOAD_MAX`). |
| `0x7FFE` | Ready Sentinel | Read | `0xA5` while a result stands, otherwise exchange buffer. Half of the reply: a bus nobody drives reads the same value here and at `0x7FFF`. |
| `0x7FFF` | Command Register | Write | Triggers API execution (`CART_CMD`). |
| `0x7FFF` | Status Register | Read | Returns the result (`last_cmd_result`) of the last command. The answer repeats until the next command write; reading it has no side effect. |

*Notes:*
* *Writing* to `0x6000 - 0x7FFE` is always intercepted by the Pico and stored in its internal `exchange_buffer`, even when the buffer is closed.
* *Reading* from `0x6000 - 0x7FFD` is only possible when a command explicitly opens the buffer (`return_buffer = true`). When the buffer is closed, reads in this range return `0xFF`. While a result stands, `0x7FFE` returns the ready sentinel even if the buffer is open.

---

## Status / Error Codes

Reading from `0x7FFF` returns the status byte of the last command. The result repeats until the next command write; reading it has no side effect.

* `0x00` : OK (Success)
* `0x01..0x13` : FatFs Error Codes (e.g., File not found, Access denied)
  * **Exception:** for `CMD_DIR_READ_NEXT` and `CMD_DIR_SEEK`, `0x01` means "end of directory" or "index past end", not a FatFs error.
* `0xFE` : Internal firmware error, unknown command, mount failure, or write mismatch
* `0xFF` : Busy / result not yet available

---

## 1. System Commands

### `0x00` - CMD_CLOSE_BUFFER
Disables read access to the Exchange Buffer.
* **Input:** None.
* **Output:** Status `0x00`. Reads from `0x6000-0x7FFD` return `0xFF`; `0x7FFE` remains the ready sentinel while the result stands.
* **Usage:** Must be called immediately after the Z80 finishes reading data requested by `CMD_FILE_READ_BLOCK` or `CMD_DIR_READ_NEXT` to prevent MSX BIOS interrupt crashes.

### `0x01` - CMD_MAP_FILE
Mounts a ROM file from the internal flash filesystem, completely replacing the current cartridge mapping.
* **Input:** `0x6000` = Null-terminated string containing the file path (e.g., `game.rom\0`).
* **Output:** Status `0x00` on success, `0xFE` on failure.
* **Usage:** The Z80 must execute this command from a RAM stub (`0xC000+`) to survive the ROM swap. After a successful mount the Pico starts emulating the loaded cartridge.

### `0x02` - CMD_MAP_SCC
Switches the cartridge to a bare SCC (Sound Custom Chip) cartridge with no ROM.
* **Input:** None.
* **Output:** Status `0x00` on success, `0xFE` on failure.
* **Usage:** Used by the preloader fallback and by the loader when `scc_on_exit=1` is set in `loader.ini`. After this command the cartridge slot still responds to SCC I/O (`0x9000` mode select, `0x9800-0x9FFF` registers) but contains no ROM, so the MSX BIOS continues booting and ends up in BASIC.

### `0x03` - CMD_MAP_DISK
Loads and patches a Disk ROM dump, mounts the `.dsk` image named in the exchange buffer, and switches the cartridge to the disk interface (`CDiskCartridge`) — all in one round trip.
* **Input:** `0x6000` = Null-terminated path to the `.dsk` image for **drive A**, optionally followed by two more null-terminated strings, in order: the Disk ROM dump to patch, and the image for **drive B**. An empty or absent second string means `disk.rom`; an empty or absent third one means drive B has no disk. The menu fills the second from `loader.ini`'s `diskrom=`, which is how an image can be paired with the dump from the machine it is meant to run on — the National and Yamaha dumps differ in Disk BASIC and in where their controller registers live — and the third from `disk2=`.

  A drive-B image that will not mount is **not** a failure: the command succeeds with drive B empty, because a stale `disk2=` should not turn into a cartridge that will not start. Which drive a later disk command means is carried in the parameter block's drive byte (`0x7E04`); a drive with no image answers status `0x02`, not ready.
* **Output:** Status `0x00` on success, `0xFE` on failure (missing/wrong-size `disk.rom`, a `disk.rom` that fails validation or has no room for the dispatcher stub, or a `.dsk` that fails to open or whose size is not a non-zero multiple of 512 bytes). Note that this is a check on the file, not on disk geometry: any sector-aligned `.dsk` mounts, standard MSX layout or not.
* **Usage:** Like `CMD_MAP_FILE`, the Z80 must execute this command from a RAM stub to survive the ROM swap. Unlike a plain ROM, the new cartridge cannot simply be jumped into: the MSX BIOS only calls a Disk ROM's `INIT` during its power-on slot scan, so after a successful mount the Z80 must soft-reset (`jp 0`) to make the BIOS look again and find it. See "Disk Commands" below for the rest of the disk protocol.

---

## 2. File System Commands

### `0x10` - CMD_FILE_OPEN_READ
Opens an existing file for reading and returns its size.
* **Input:** `0x6000` = Null-terminated file path.
* **Output:**
  * Status `0x00` on success, FatFs error code on failure.
  * `0x6000 - 0x6003` = 32-bit unsigned integer representing the file size in bytes (Little-Endian).
  * *Buffer is open for reading.* Z80 **must** call `CMD_CLOSE_BUFFER` after reading the size.

### `0x11` - CMD_FILE_OPEN_WRITE
Opens a file for writing, creating it if it does not exist or truncating it if it does.
* **Input:** `0x6000` = Null-terminated file path.
* **Output:** Status `0x00` if successful, or FatFs error code. Buffer remains closed.

### `0x12` - CMD_FILE_READ_BLOCK
Reads a chunk of data from the opened file into the Exchange Buffer and **opens the buffer** for reading.
* **Input:** None.
* **Output:**
  * Status `0x00` on success, FatFs error code on failure.
  * `0x6000 (Low), 0x6001 (High)` = Number of bytes actually read (`0` means EOF).
  * `0x6002 ...` = File payload.
  * Maximum payload per call is `0x2000 - 4 = 8188` bytes (`LOADER_PAYLOAD_MAX`): two bytes hold the length and the top two addresses are reserved for the reply.
  * *Buffer is open for reading.* Z80 **must** call `CMD_CLOSE_BUFFER` after copying the data.

### `0x13` - CMD_FILE_WRITE_BLOCK
Writes a chunk of data from the Exchange Buffer to the opened file.
* **Input:**
  * `0x6000 (Low), 0x6001 (High)` = Number of bytes to write (max `8188`; larger values are clamped).
  * `0x6002 ...` = File payload to write.
* **Output:** Status `0x00` on success, FatFs error code on failure, or `0xFE` if the written byte count did not match. Buffer remains closed.

### `0x14` - CMD_FILE_CLOSE
Closes the currently opened file, flushing writes to flash.
* **Input:** None.
* **Output:** Status `0x00` or FatFs error code.

---

## 3. Directory Navigation

### `0x20` - CMD_DIR_OPEN
Opens a directory for iteration.
* **Input:** `0x6000` = Null-terminated directory path (e.g., `/\0` or `/GAMES\0`).
* **Output:** Status `0x00` if successful, or FatFs error code.

### `0x21` - CMD_DIR_READ_NEXT
Reads the next visible entry in the opened directory and **opens the buffer** for reading. Hidden and system files are skipped.
* **Input:** None.
* **Output:**
  * Status `0x00` on success.
  * Status `0x01` when there are no more files (End of Directory).
  * `0x6000` = Attribute flag (`0x00` = File, `0x01` = Directory).
  * `0x6001 ...` = Null-terminated string with the file/folder name.
  * Immediately after that terminator: a second null-terminated string, the entry's 8.3 alias, **empty when it is the same as the name above**. A menu that shows long names has to truncate them to its column, and a truncated long name cannot be opened again; the alias always can. `msx/loader.asm` displays the first string and opens the second.
  * *Buffer is open for reading.* Z80 **must** call `CMD_CLOSE_BUFFER` after reading the name.

### `0x22` - CMD_DIR_SEEK
Moves the current directory pointer to the selected visible entry. Hidden and system files are skipped when counting entries.
* **Input:**
  * `0x6000 (Low), 0x6001 (High)` = 2-byte index of the visible entry to seek to.
* **Output:** Status `0x00` if successful, `0x01` if index is past the end, or FatFs error code.

### `0x23` - CMD_DIR_CLOSE
Closes the directory handle.
* **Input:** None.
* **Output:** Status `0x00` or FatFs error code.

---

## 4. Disk Commands

Available only after `CMD_MAP_DISK` (`0x03`, above) has switched the cartridge
to the disk interface (`CDiskCartridge`). Parameters live at the top of the
exchange buffer so that sector data starts at `0x6000` and the Z80 side is a
plain `ld hl,0x6000` / `ldir`.

| Offset | Field |
|---|---|
| `0x7E00`-`0x7E02` | LBA, little-endian, 24 bits |
| `0x7E03` | sector count, 1..12, for a read and a write alike |
| `0x7E04` | drive number |
| `0x7E05` | flags (status output only): bit 0 disk changed, bit 1 write protected |

Two more ranges are served conditionally, only while the code that lives
there is the thing currently running; otherwise a read in them falls through
to the Disk ROM image underneath, unchanged:

| Range | Contents |
|---|---|
| `0x6000 - 0x75FF` | Sector data: 11 sectors (`DISK_MAX_SECTORS`) |
| `0x7600 - 0x7BFF` | BASIC window (entry remains `0x7800`): the Disk BASIC statement handler, open only for the length of one `CALL MOUNT`/`EJECT`/`DISK`/`DIR`/`ODEL`/`OCOPY`/`ONEW` |
| `0x7C00 - 0x7DFF` | Code window: the dispatcher blob, open only while `DSKIO`/`DSKCHG` is running (`0x37`/`0x38` below) |

There is no separate "mount" command: `CMD_MAP_DISK` (`0x03`, above) loads and
patches `disk.rom` and opens the `.dsk` image as part of switching
cartridges, in one round trip, because a disk cartridge cannot be constructed
without an image.

**How a disk command replies.** Once a command's result is ready, `0x7FFF`
answers `0xA5` — the ready token — and `0x7FFE` answers the status. Both keep
answering until the next command write; neither read has a side effect. The Z80
poll reads *both*: it waits until `0x7FFF` reads `0xA5` **and** `0x7FFE` reads
something that is not `0xA5`, then takes `0x7FFE` as the status.

**Which commands hold `/WAIT`, and why it decides where the poll may live.**
Two regimes apply: fast handlers hold `/WAIT`, while handlers that access files or flash release it.

Fast disk commands are answered with `/WAIT` still
asserted. The Z80 stays stalled inside its write to `0x7FFF` until the result
stands, so it never reads an unserved bus and the poll that follows is a
formality. This is affordable only because those handlers are short: a read is
capped at 11 sectors (`CHUNK_SECTORS` in `msx/diskstub.asm`), which is roughly
340 µs of held line against the ~2 ms in which MSX DRAM wants every row
refreshed — a stalled Z80 issues no M1 cycles and so refreshes nothing.

The BASIC statement commands `CMD_BASIC_MOUNT`, `CMD_BASIC_EJECT`,
`CMD_BASIC_DIR_OPEN` and `CMD_BASIC_DIR_NEXT` also release `/WAIT`: a file
opened, a directory walked or a path followed is unbounded work.

`CMD_DISK_WRITE`, `CMD_DISK_FLUSH` and `CMD_DISK_FORMAT` cannot be made short.
Programming one 512-byte page is most of a millisecond, every eighth page
erases a 4 KB flash sector, and an erase is tens of milliseconds — ten to
fifty times the refresh budget. All three therefore release `/WAIT` before
they touch flash, and for the length of the handler the cartridge serves
nothing at all.

That is what decides where the poll loop may live. A loop served from the code
window would be fetching its own instructions from an undriven bus; the driver
polls these slow commands from `MailCmd`, which is part of the trampoline in MSX
RAM, and its fetches are unaffected. Its *reads* still land on the unserved
cartridge, which is what the two-address gate below is for. The Z80 running
that loop also keeps issuing M1 cycles, so DRAM is refreshed throughout.

Reading readiness off the bus is the trap. An undriven MSX data bus does not
read back as `0xFF` — it floats at its last driven value, which is the last
sector byte the previous transfer read out. For binary data that byte is `0xA5`
about as often as anything else, so a poll that waits only for
`0x7FFF == 0xA5` can match a floating bus and return early onto a buffer nobody
filled. The failure scales with the number of unserved reads and looks random
because it depends on the data.

The fix uses the one thing that separates a driven bus from a floating one: a
floating bus is monochromatic — nothing drives it, so every address reads the
same held value — while a driven bus is not. Requiring `0x7FFF == 0xA5` **and**
`0x7FFE != 0xA5` can only be satisfied by the firmware actually driving both,
because a floating bus that reads `0xA5` at `0x7FFF` reads `0xA5` at `0x7FFE`
too. No status this firmware produces is `0xA5` — the set it sends is
`0x00`/`0x02`/`0x04`/`0x06`/`0x08`/`0x0A`/`0xFE`, and `test_constants.py`
resolves every served status and checks it — so a genuine reply always clears
the second test. The driver's own `ST_TIMEOUT` (`0x0C`), which `MailCmd` answers
when the firmware never does, is MSX-side only and is likewise not `0xA5`.
Both answers repeat until the next command, so a read lost to a missed cycle
just comes round again. `CRomPatcher::Apply` still pins the image's last byte to
`0xFF` so the idle `0x7FFF` cannot read as the token when no command is in
flight; exchange-buffer byte `0x1FFE` is shadowed while a result stands, and
nothing else reads it.

The emulator always drives the bus, so every read is answered in the cycle it
is issued and the floating-bus failure cannot occur or be tested there. Only the
MSX-side gate defends against it.

The loader commands in sections 2 and 3 are gated the same way, with the roles
of the two addresses swapped: the sentinel lives at `0x7FFE` and the status at
`0x7FFF`. The split is historical rather than meaningful. See "How a loader
command replies" above for what the ungated version did on hardware.

**How the bus loop comes back from a slow handler.** It waits for `/SLTSL` to
rise before it looks for the next cycle. The loop only ever serves cycles it
saw begin — `/WAIT` goes low within a few tens of nanoseconds of `/SLTSL`,
well before the Z80 samples it — and that holds by construction everywhere
except on return from a handler, when the Z80 has been polling `0x7FFF`
unattended and the loop's `/SLTSL` test passes just as readily on a read that
is already half over. Serving that read is a race the firmware cannot win:
`/WAIT` is too late, the byte is driven some 250 ns after the sample, and if
that lands after the read has ended it lands on the opcode fetch from MSX RAM
that follows every poll read, with the cartridge and the RAM both driving the
bus and the Z80 latching the collision as an instruction. The odds per command
are small; a long load issues thousands. Waiting for `/SLTSL` first turns a
cycle caught in flight into a cycle missed cleanly, which the poll absorbs as
one more iteration. The loader loop has the same wait for the same reason.

### `0x31` - CMD_DISK_READ
Reads `count` consecutive sectors starting at `LBA` into the exchange buffer and opens the buffer for reading.
* **Input:** `0x7E00-0x7E02` = LBA, `0x7E03` = sector count (1-11).
* **Output:** Status `0x00` on success, `0x04` (data error) if the underlying read failed, `0xFE` if `count` is `0` or greater than `11`. On success sector data starts at `0x6000`. Z80 **must** call `CMD_CLOSE_BUFFER` after copying it out. The cap is 11 because the sector data area ends where the BASIC window begins at `0x7600` — below the code window at `0x7C00` — and a longer read would write over the Disk BASIC statement handler served there.

### `0x32` - CMD_DISK_WRITE
Writes `count` consecutive sectors from the exchange buffer to `LBA`, in place.
* **Input:** `0x7E00-0x7E02` = LBA, `0x7E03` = sector count (1-11), sector data at `0x6000`. No command opens the buffer for this: buffer-range writes are captured on every bus cycle, so the driver fills it with a plain `LDIR` and then sends this.
* **Output:** Status `0x00` on success, `0x0A` if the image is write protected, `0x04` if the write itself failed, `0xFE` if `count` is `0` or greater than `11`. The driver maps `0x0A` to `DSKIO` error code `0` (write protected) and `0x04` to code `10` (write fault), because the first tells the caller not to retry.
* **Usage:** Answered with `/WAIT` **released** — see the two regimes above — so the Z80 must poll for it from `MailCmd` in MSX RAM, never from the code window. The write reaches flash, but the filesystem's own sector map does not: issue `CMD_DISK_FLUSH` once the transfer is finished. Only sector contents change; the image keeps its size and its cluster chain, which is what lets the firmware write straight to flash past FatFs and keeps the direct LBA map valid.

### `0x39` - CMD_DISK_FORMAT
Writes a fresh MSX filesystem over the mounted image.
* **Input:** `0x7E04` = drive number. Nothing else: the layout comes from the image's own size.
* **Output:** Status `0x00` on success, `0x0A` if the image is write protected, `0x04` for anything else — a write failure, or a size whose BPB this project will not invent. The driver reports the last two as `DSKFMT` error `12` ("other error") and write protection as `0`.
* **Usage:** Boot sector, both FATs and an empty root directory; the data area is left alone, as a real floppy format leaves it. Only 720-sector (360 KB, media `0xF8`) and 1440-sector (720 KB, media `0xF9`) images can be formatted — see `include/DiskFormat.hxx`, which the cartridge, the openMSX device and the host test all build the same bytes from. A successful format raises the disk-changed flag, so the next `DSKCHG` reports a media change and sends the caller back to `GETDPB` for the geometry just written. Answered with `/WAIT` released, like the other two flash commands.

### `0x34` - CMD_DISK_FLUSH
Persists what a write left in the Pico's RAM.
* **Input:** None.
* **Output:** Status `0x00` on success, `0x04` if the flush failed.
* **Usage:** Sent once at the end of a write call, not once per chunk: the map lives in a single 4 KB flash sector, so syncing per chunk would erase that sector once per 512 bytes written. Without it a power cycle loses both the new data and the sectors it replaced, because the map still points at pages the write had already freed. Answered with `/WAIT` released, like `CMD_DISK_WRITE`.

### `0x33` - CMD_DISK_STATUS
Fills the flags byte at `0x7E05` and opens the buffer for reading.
* **Input:** None.
* **Output:** Status always `0x00`. `0x7E05` bit 0 = disk changed since the last query (this call consumes the flag, so a fresh mount is reported exactly once), bit 1 = write protected — set when the volume would not open the image for writing, which is what the read-only attribute on the file means here. Z80 **must** call `CMD_CLOSE_BUFFER` after reading it.

### `0x35` - CMD_DISK_SETRAM
Reports the MSX RAM address the `INIT` tail just reserved, and places a relocated copy of the trampoline blob in the exchange buffer for the tail to `ldir` out.
* **Input:** `0x7E00-0x7E01` = 16-bit MSX address of the base of the reserved RAM area (in the same LBA-low/mid bytes the read commands use). `0x7E06-0x7E07` = the handshake `A5 5A`.
* **Output:** Status `0x00` on success, `0xFE` if the handshake is absent. The trampoline lives at the reported base itself, and the reservation is the trampoline blob plus one byte for the cartridge's own slot id — nothing else. The dispatcher proper is not in MSX RAM at all: it is served through the code window (`0x37`), and the trampoline only opens that window around each disk call and owns the page-1 transfer path, which cannot run in the window because reaching a page-1 destination means handing page 1 to the RAM slot and the window would go with it. That matters because the whole reservation is taken out of MSX RAM below `HIMEM` for the life of the session, and BASIC derives `STKTOP`, `MEMSIZ` and its string space from `HIMEM`, so every byte reserved is a byte lower that a disk's `BLOAD` image may land relative to BASIC's stack. It is why the page-1 transfer path stages its bytes on the MSX stack rather than in a bounce buffer reserved here: the stack is in page 3, so it is addressable whichever slot owns page 1, and it costs nothing on the disks that never take that path. The firmware repoints the patched Disk BIOS jump table entries at the base, relocates a copy of the dispatcher blob for that address into the base of the exchange buffer, opens the buffer for reading, and clears the handshake bytes. Z80 **must** call `CMD_CLOSE_BUFFER` after copying the dispatcher out.
* **Usage:** Issued by the `INIT` tail in `msx/diskstub.asm`, from MSX RAM, *after* the vendor `INIT` has returned — which is why the handshake exists. Vendor `INIT` code writes to what it believes are its own FDC registers, and `0x7FFF` is one of them on plenty of interfaces, so a stray `0x35` can now reach the command register before the real one; without the handshake a plain one-shot guard would spend itself on the stray write and silently refuse the command that mattered. The firmware runs this handler on its fast path, with `/WAIT` asserted and interrupts disabled, because the work is bounded (a `memcpy` and a relocation pass); a future command must not be added there unless its total work stays bounded, file-I/O-free and low-microsecond. Since the `INIT` prologue stopped polling from cartridge ROM, no command in the protocol depends on the bus staying served while a handler runs — but any future ROM-resident caller would bring that requirement straight back.

### `0x37` / `0x38` - CMD_DISK_WINDOW_ON / CMD_DISK_WINDOW_OFF
Open and close the code window: while it is open the firmware answers reads of `0x7C00-0x7DFF` out of the exchange buffer, where the dispatcher blob sits, instead of out of the Disk ROM image.
* **Input:** None.
* **Output:** Status always `0x00`. Neither blocks — each is a flag and nothing else — so the write that carries the command is the whole handshake and there is nothing to poll for. Like every command they clear the sector overlay (`return_buffer`), but the window flag itself is independent of it: the dispatcher goes on executing across the reads that open and close the overlay.
* **Usage:** Issued by the trampoline in MSX RAM, on the way into and out of every `DSKIO` and `DSKCHG`, under `DI`. The window must never be left open across a return to the caller: the vendor bytes underneath `0x7C00` are live Disk BASIC in both dumps — an opcode-anchored scan finds 14 references into that range in the National image and 24 in the Yamaha one — so hiding them permanently would break Disk BASIC. Opening it only while our own code is the only thing running is what makes serving the dispatcher from the cartridge safe, and that in turn is what shrank the MSX RAM reservation to the trampoline alone.

### `0x36` - CMD_DISK_UNBORROW
Gives the borrowed ROM window back to the vendor image.
* **Input:** None.
* **Output:** Status always `0x00`. The `stubLen` bytes at `0x4022` are exchanged with the vendor bytes `CRomPatcher::Apply` saved when it opened the window, so from this point the served image is byte-identical to the dump apart from the Disk BIOS jump table thunks and the `INIT` vector. Issuing it twice is harmless: the firmware tracks which version is installed and ignores the second call.
* **Usage:** Issued by the `INIT` tail as its first act once that tail is running from MSX RAM, before a single vendor instruction runs — the vendor executes code inside the window, so it has to go back before the vendor `INIT` is entered, not after. It cannot be issued from the ROM part: the bytes underneath that code are exactly the ones the handler rewrites. Like `CMD_DISK_SETRAM` it runs on the firmware's fast path, with `/WAIT` held, so the window's contents cannot change under a Z80 that is still executing; the same bounded, file-I/O-free rule applies to anything added there.

**Why a borrow at all.** Real Disk ROMs have no free space. The two dumps this was first tried against — a National and a Yamaha YIS805, 80% identical Disk BASIC over different FDC drivers — hold at most 195 and 26 contiguous filler bytes against the ~200 the ROM stub needs, and the bodies of the routines the patcher replaces turn out to be full of entry points the rest of the ROM calls. So the space is borrowed only from the BIOS's header scan to the `INIT` tail's first instruction in MSX RAM, during which the only code executing from page 1 is the prologue itself, and then returned. Both dumps call into that window from elsewhere — 33 and 34 references respectively — so returning it intact is load-bearing, and returning it *before* the vendor `INIT` runs is mandatory. The firmware re-arms the window when it sees the BIOS fetch the `INIT` vector — four reads of `0x4000`-`0x4003` in a row — which is what makes a second MSX reset work; no `/RESET` line reaches the Pico, so the bus is the only signal available.

**Why the allocation happens after the vendor `INIT`.** A Disk ROM's `INIT` assumes every byte below its own `HIMEM` is free: it allocates downward from `HIMEM`, it zeroes work areas at hard-coded addresses near the top of RAM (the Yamaha image zeroes `F1C9`-`F37F` from constants written into the ROM), and at least one of its allocations sizes itself from a DPB it has not filled in yet. Reserving before it ran corrupted that arithmetic and both dumps died at the same allocator with "No enough memory" — for one padding value out of two, which made padding a lottery over uninitialised memory rather than a fix. Reserving after it has returned is the ordinary thing a second cartridge does, and needs no guesses about the vendor's layout.

### `0x3A` - CMD_BASIC_WINDOW_OFF
Closes the BASIC window: the firmware goes back to answering `0x7600`-`0x7BFF` out of the Disk ROM image instead of out of the exchange buffer, where the Disk BASIC statement handler sits.
* **Input:** None.
* **Output:** Status always `0x00`. A flag and nothing else, like `0x37`/`0x38`, so the write that carries it is the whole handshake and there is nothing to poll for.
* **Usage:** There is no matching open. BASIC reaches the statement handler through `CALSLT` from the ROM header vector at `0x4004`, so there is no chance to ask for the window first: the bus loop arms itself on a read of `0x4004` immediately followed by a read of `0x4005`, with no other cartridge bus cycle between — measured in openMSX on both supported Disk ROM dumps, no `0x4000`/`0x4001` prefix (design spec, `## Measured`) — and opens the window on the very next cycle if that is a fetch of `0x7800` (see `RomStatementScanStep`). Only the close is the driver's, and it can only be issued from MSX RAM: the instruction fetched after this write already comes from the vendor's bytes. See `BasicClose` in `msx/diskstub.asm`. Leaving it open would hide live Disk BASIC — the bytes underneath are the vendor's own, the same as for the dispatcher's window, and on both supported dumps they include the `H.TIMI` motor-off timer every VDP tick reaches for (`0x7837` National, `0x7827` Yamaha), which is why the handler writes a `ret` over `H.KEYI`/`H.TIMI` at entry and restores both before this command is sent (see `BasEntry` and `BasHooksBack` in `msx/diskstub.asm`).

### `0x3B` - CMD_BASIC_MOUNT
`CALL MOUNT("[X:]path")`: mounts an image into a drive on a running machine.
* **Input:** `0x6000` = the path, NUL-terminated, at most 64 characters (`DISK_BASIC_PATH_MAX`); `0x7E04` = drive number. No command opens the buffer to write the path: buffer-range writes are captured on every bus cycle. The drive letter is not part of the path — the driver takes an `A:`/`B:` prefix off it and puts the number here, and refuses any other letter with `NO SUCH DRIVE` before sending anything.
* **Output:** Status `0x00` on success, `0x06` (`DISK_ST_NOFILE`) when the path names no file the volume can open, names a directory, or names a file that is not a non-zero whole number of 512-byte sectors — checked with `f_stat` before `Mount()` is called, so a refused path leaves the drive's current image in place rather than ejecting it — `0x02` for a drive number that is neither 0 nor 1.
* **Usage:** Replaces whatever was mounted in that drive and raises the disk-changed flag, so the next `DSKCHG` reports a media change and sends Disk BASIC back to `GETDPB` — which is what lets a 360 KB image replace a 720 KB one mid-session without rebooting. Answered with `/WAIT` **released**, like the flash commands: opening a file and building its CLMT is unbounded file I/O, so the Z80 must poll for the result from `MailCmd` in MSX RAM, never from either window.

### `0x3C` - CMD_BASIC_EJECT
`CALL EJECT(["X:"])`: leaves a drive empty.
* **Input:** `0x7E04` = drive number.
* **Output:** Status `0x00`, including for a drive that was already empty — the statement asks for a state, not for a transition. `0x02` for a drive number that is neither 0 nor 1.
* **Usage:** Sector commands for the drive then answer `0x02` (not ready), which is what drive B is whenever `loader.ini` names no `disk2=`. Answered with `/WAIT` released, like the mount: closing a file that was written to flushes it.

### `0x3D` - CMD_BASIC_DIR_OPEN
`CALL DIR(["path"])`: opens a directory of the cartridge's own FAT volume so that `0x3E` can step it. This is about the volume, not about either drive — the mounted images are files *on* it — so no drive number comes into it.
* **Input:** `0x6000` = the directory path, NUL-terminated and empty for the root, at most 64 characters (`DISK_BASIC_PATH_MAX`). No command opens the buffer to write it: buffer-range writes are captured on every bus cycle.
* **Output:** Status `0x00` when the directory is open, `0x06` (`DISK_ST_NOFILE`) when the path names nothing the volume can open as one.
* **Usage:** The handle it opens lives until `0x3E` walks off the end of the directory, until the statement's `CMD_BASIC_WINDOW_OFF` collects it, or until the bus loop sees the BIOS's header scan and drops it with the windows. Answered with `/WAIT` **released**, like the mount: following a path is a linear scan of one directory per component, which on a full volume is nothing like bounded, so the Z80 polls for it from `MailCmd` in MSX RAM.

### `0x3E` - CMD_BASIC_DIR_NEXT
Steps the open directory by one visible entry and **opens the buffer** for reading.
* **Input:** None.
* **Output:**
  * Status `0x00` with the entry in the exchange buffer, `0x08` (`DISK_ST_ENDDIR`) once there are no more, `0xFE` for a step with no open behind it or a read the volume failed.

  | Offset | Size | Meaning |
  | :--- | :--- | :--- |
  | +0 | 1 | Attribute: bit 0 set when the entry is a directory |
  | +1 | *n*+1 | The entry's name, NUL-terminated, at most `DISK_BASIC_PATH_MAX` bytes |

  The name is the entry's long name when that is at most `DISK_DIR_NAME_MAX` (32) characters and its 8.3 alias otherwise — the rule `0x21`'s consumer in `msx/loader.asm` applies to the browser (`NAME_MAX`, alias fallback). The driver prints the name in a field exactly `DISK_DIR_NAME_MAX` wide (`DIR_NAME_MAX` in `diskstub.asm`; `test_constants.py` ties the two), and FatFs opens long names as they are, so what `CALL DIR` shows is always the whole of a name that `CALL MOUNT` will accept; the alias is only for a name off a PC that the field could not show whole. When FatFs has no alias to give (an exFAT volume), the long name is sent whatever its length and the driver cuts it to the field. Hidden and system entries are skipped, so a volume that has been near a Mac does not list a screenful of `._` files, and so is an entry that is neither a directory nor a name ending in `.dsk` (matched case-insensitively) — `CALL MOUNT` never opens anything else, so a ROM, `loader.ini` or `disk.rom` on the volume would only be noise in this listing.
* **Usage:** The firmware closes the directory as it answers `DISK_ST_ENDDIR`, so a listing that runs to its end owes nothing afterwards. Answered with `/WAIT` released, like the open. The driver reads each entry through the overlay this opens, prints it, and steps again — the next command write is what closes that overlay, and the statement goes on executing out of the BASIC window across all of it.

### `0x3F` - CMD_BASIC_INFO
`CALL DISK`: reports what is in each drive, both in one reply.
* **Input:** None. The statement takes no argument and the reply covers both drives, so there is no drive number to pass.
* **Output:** Status `0x00`, and the exchange buffer opened for reading with one record per drive from `0x6000`, in drive order:

  | Offset | Size | Meaning |
  | :--- | :--- | :--- |
  | +0 | 1 | State: bit 0 set when a disk is mounted, bit 1 when it is write-protected |
  | +1 | 2 | Sector count, little-endian, clamped to `0xFFFF` |
  | +3 | *n*+1 | The path the image was mounted from, NUL-terminated; empty for an empty drive |

  Drive B's record starts immediately after drive A's terminator. An empty drive still gets all three fields, because the driver walks a record to reach the next one. The count is clamped rather than wrapped: the sizes this is for are 720 and 1440 sectors, but `CDiskImage::Mount` whitelists no size at all, and a wrapped count would read on screen as a plausible small disk.
* **Usage:** Answered on the fast path, with `/WAIT` still **asserted** — unlike `0x3B` and `0x3C` beside it. Nothing here opens a file: `Mounted()`, `ReadOnly()` and `SectorCount()` read members and `Path()` hands back the string `Mount()` kept, so the work is two short `memcpy`s and stays inside the bounded, file-I/O-free rule for anything served with the Z80 stalled. The driver reads the reply through the overlay this opens and lets the statement's own `CMD_BASIC_WINDOW_OFF` close it — that write clears `return_buffer`, and every path out of the handler takes it.

---

## 5. `loader.ini` Options

The MSX-side `loader.ini` file controls the boot menu. Which keys apply depends on which menu ROM is on the volume as `loader.rom` — the only name the firmware loads — and the two schemas are **mutually exclusive**: each menu owns the file and reads only its own keys.

| On the volume as `loader.rom` | Built from | Schema |
| :--- | :--- | :--- |
| `generated/loader.rom` | `msx/loader.asm` | Browser over the volume (below) |
| `generated/loader_fav.rom` | `msx/loader_fav.asm` | Ten fixed favourites (further down) |

### Browser menu (`loader.rom`)

The browser **rewrites the whole file** every time an entry is booted: the five keys below plus the `name=` lines, in the order they were written. Comments and unknown keys are lost on that first rewrite, which is the price of never having to edit the file by hand. All options are optional.

| Option | Values | Description |
| :--- | :--- | :--- |
| `last=<path>` | Path | Entry booted last: the auto-boot target, and the directory the browser opens in. A name ending in `.DSK` (case-insensitive) is mounted as a disk image, anything else boots as a ROM. |
| `disk2=<path>` | Path | Image for drive B, mounted alongside `last` whenever that boots a `.dsk`. Written by `SPACE` in the browser. An image that will not open leaves drive B empty rather than failing the boot. |
| `delay=<ms>` | Positive integer | Auto-boot countdown in milliseconds (default: 500). |
| `diskrom=<path>` | Path, at most 64 characters | Disk ROM dump `CMD_MAP_DISK` should patch, instead of the firmware's default `disk.rom`. A path, so the dump may live in a folder. Empty means the default. |
| `scc_on_exit=<0\|1>` | `0` or `1` | If `1`, pressing ESC in the menu switches the cartridge to bare SCC before returning to BASIC. |
| `name=<file>,<title>` | Repeatable | Lists `<file>` under `<title>`. Matched on the filename alone, case-insensitively, so one line covers that name in any directory; the real filename is still what gets opened, mapper tag included. |

#### Example

```ini
last=/games/mg[KonamiSCC].rom
disk2=/games/side-b.dsk
delay=500
diskrom=/roms/yis805-128r2_disk.rom
scc_on_exit=1
name=mg[KonamiSCC].rom,Metal Gear
name=sd-snatcher[SCC].rom,SD Snatcher
```

### Favourites menu (`loader_fav.rom`)

This menu only ever rewrites the single digit after `default=`; the rest of the file is left exactly as written. All options are optional.

| Option | Values | Description |
| :--- | :--- | :--- |
| `favN=<file>[,label]` | `N` = `0..9` | Defines a favourite menu entry. The file is a ROM or a `.dsk` disk image located in the flash root — a name ending in `.DSK` (case-insensitive) is mounted as a disk image, anything else boots as a ROM. Optional `label` overrides the displayed text. |
| `default=<N>` | `0..9` | Slot selected by default and auto-booted after the delay expires. |
| `delay=<ms>` | Positive integer | Auto-boot countdown in milliseconds (default: 500). |
| `diskrom=<path>` | Path, at most 64 characters | As above: the Disk ROM dump to patch instead of `disk.rom`. |
| `scc_on_exit=<0\|1>` | `0` or `1` | If `1`, pressing ESC in the menu switches the cartridge to bare SCC before returning to BASIC. |

#### Example

```ini
fav0=gradius.rom,Gradius
fav1=kingvalley.rom
fav2=metalgear.rom
fav3=game.dsk,My Game

default=0
delay=3000
scc_on_exit=1
```

---

## 6. Preloader Boot Flow

On power-on the Pico first emulates an 8 KB preloader ROM at `0x4000`. The preloader:

1. Copies `loader.rom` name to the exchange buffer.
2. Sends `CMD_MAP_FILE`.
3. Waits for the result.
4. On success: waits briefly, then jumps through the loaded ROM's init vector at `0x4002`.
5. On failure: sends `CMD_MAP_SCC`, waits for the result, then returns to the MSX BIOS. Because the SCC cartridge has no ROM header, the BIOS continues booting and ends up in BASIC with SCC I/O still active.

---

## 7. Z80 Implementation Example: Reading an INI File

Run this routine and its filename string from MSX RAM (for example, copy them there before calling it).
The poll uses both reply addresses because an undriven bus can echo a command
byte. A production caller should also bound the poll and report a timeout, as
`msx/mailbox.inc` does.

```asm
CART_BUF    EQU $6000
CART_READY  EQU $7FFE
CART_CMD    EQU $7FFF

; This routine and SendCommand must already be in MSX RAM.
ReadIni:
    ld hl, FileName
    ld de, CART_BUF
    ld bc, FileNameEnd - FileName
    ldir

    ld a, $10                   ; CMD_FILE_OPEN_READ
    call SendCommand
    or a
    jp nz, Error
    ; CART_BUF..CART_BUF+3 now holds the 32-bit file size.
    ld a, $00                   ; CMD_CLOSE_BUFFER
    call SendCommand

    ld a, $12                   ; CMD_FILE_READ_BLOCK
    call SendCommand
    or a
    jp nz, Error

    ld a, (CART_BUF)            ; bytes read, low
    ld e, a
    ld a, (CART_BUF+1)          ; bytes read, high
    ld d, a
    ld hl, CART_BUF+2           ; payload starts here
    ; Copy or process DE bytes from HL before the next command.

    ld a, $00                   ; CMD_CLOSE_BUFFER
    call SendCommand
    ld a, $14                   ; CMD_FILE_CLOSE
    call SendCommand
    ret

; A = command on entry; A = status on exit. Must execute from MSX RAM.
SendCommand:
    ld (CART_CMD), a
.wait:
    ld a, (CART_READY)
    cp $A5
    jr nz, .wait
    ld a, (CART_CMD)
    cp $A5                      ; same value at both addresses: floating bus
    jr z, .wait
    ret

FileName:    db "loader.ini", 0
FileNameEnd:
```

The result at `0x7FFF` repeats until the next command write. Close the buffer
after reading each reply, before an interrupt can read the overlay.


## Outer-volume DSK file commands

These disk-mode commands back BASIC `CALL ODEL`, `CALL OCOPY`, `CALL ONEW`
and the corresponding DOS COMs. They release `/WAIT` before filesystem I/O;
the Z80 must poll from RAM. They never mount or eject images.

| Command | Input | Result |
|---|---|---|
| `0x40` - CMD_FILE_DELETE | NUL-terminated path at `0x6000` | Delete the named DSK; refuse a mounted file; reply is final |
| `0x41` - CMD_FILE_COPY | Source at `0x6000`, destination at `0x6041` | Open source and exclusively create destination; return `0x10` |
| `0x42` - CMD_FILE_NEW | Path at `0x6000`, size in KiB as little-endian uint16 at `0x6082` | Exclusively create a 360 or 720 KiB image; return `0x10` |
| `0x43` - CMD_FILE_STEP | None | Transfer one 512-byte sector; `0x10` while more remain, `0x00` after close/sync |

Each path slot is 65 bytes including NUL, with at most 64 bytes of path.
These commands require `.dsk` (case insensitive), with a nonempty basename;
no drive prefix, wildcards, `.`/`..` components, or trailing dots/spaces in
directory names. Subdirectories and spaces within quoted names are supported.
The optional leading slash addresses the same outer-volume root.

Status codes: `0x00` success, `0x04` I/O error, `0x06` missing path,
`0x0A` write protected/access denied, `0x10` continue with STEP, `0x12` target
exists, `0x14` image in use, `0x16` invalid argument/no active transfer,
`0x18` disk full. Replies are status-only. Do not issue another STEP after
an error. `CMD_CLOSE_BUFFER` (`0x00`) between steps does not abort a transfer.
`CMD_FILE_DELETE` completes in one reply; `CMD_FILE_COPY` and `CMD_FILE_NEW`
must be followed by `CMD_FILE_STEP` until the result is no longer `0x10`.

COPY synchronizes the mounted image handles before opening its source. The
DOS clients first flush DOS buffers with function `0Dh`; BASIC callers must
close files before copying their mounted image. File identity for DELETE is
checked using the FAT volume and first cluster, so case changes, leading
slashes and 8.3 aliases cannot bypass the mounted-image check. Existing targets
are never overwritten, including copying a file to itself through another name.

NEW writes the shared MSX FAT12 boot sector, two initialized FATs, an empty
root directory and a zero-filled data area. It creates no DOS system files and
does not mount the result. COPY preserves all source bytes and its size.

Only one transfer may be active. Starting DELETE/COPY/NEW aborts an unfinished
transfer and attempts to remove its partial destination. Completion closes
and syncs the file; an I/O or capacity error also attempts cleanup. Persistent
media errors or a power loss can leave an incomplete destination; they cannot
be rolled back atomically. A client timeout must not send another command
until the cartridge has finished its current request. The next file operation
cleans up the abandoned transfer. `0x3A` only closes the BASIC window/directory;
it performs no slow transfer cleanup because its caller does not poll.
