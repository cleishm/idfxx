# idfxx_persistent_log flash format

This document describes the bytes that `idfxx_persistent_log` stores in flash. It gives enough
detail to decode an image of the log's partition on a host. It describes layout version 1. The
last section describes the state that the device keeps in RTC memory. That state is not in
flash.

## Terms

Each term below has one meaning in this document.

- **Device**: the firmware that runs `idfxx_persistent_log` and writes the store.
- **Decoder**: a program that reads an image of the store, for example on a host.
- **Store**: the flash that holds the log. It is a partition (by default the partition labeled
  `log`), or a store that the app passes in `config::storage`. An offset counts from the start of
  the store, unless the text or the table names another start, such as a sector, a record or a
  slot.
- **Sector**: an erase unit of the store. For a partition, the sector size is the partition's erase
  size, which is 4096 bytes on SPI flash.
- **Block**: 16 bytes that start at an offset that is a multiple of 16.
- **Erased**: a byte is erased when it holds `0xFF` as stored in flash.
- **Log region** and **fault region**: the two parts of the store.
- **Log sector**: a sector of the log region.
- **Sector header** and **retire block**: the first and second blocks of a log sector.
- **Record**: a length, a CRC and text in a log sector.
- **Text stream**: the text of the records, joined in order.
- **Log entry**: one stamped item of text in the text stream.
- **Slot**: a fixed-size part of the fault region that holds one fault entry.
- **Fault entry**: a slot header and its context.
- **Slot header**: the first 512 bytes of a slot.
- **Context**: the log text that a fault entry holds, after its slot header.
- **Cleared entry**: a fault entry of kind `cleared`.

## Byte order and CRC

All integers are little-endian. `u16`, `u32` and `u64` are unsigned integers of 2, 4 and 8 bytes.
`i64` is a two's-complement signed integer of 8 bytes.

Every CRC in this format is CRC-32 with these parameters: polynomial `0x04C11DB7`, reflected input
and output, initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. This is the CRC of zlib's `crc32()`
and of Python's `zlib.crc32()`. The CRC of the ASCII bytes `123456789` is `0xCBF43926`. The device
computes it with `esp_rom_crc32_le()`, starting from 0. When the device computes a CRC over several
runs of bytes, it passes each result to the next call. The result is the CRC of the runs joined
together.

| CRC | Stored at | Covers |
| --- | --------- | ------ |
| Sector header CRC | Log sector, offset 12 | Bytes 0–11 of the sector |
| Record CRC | Record, offset 2 | The record's 2 length bytes, then its text. It does not cover the CRC field or the padding. |
| Slot header CRC | Slot, offset 508 | Bytes 0–507 of the slot |
| Context CRC | Slot, offset 40 | The context's bytes (the context size). It does not cover the padding. |
| Core dump CRC | Slot, offset 360 | A complete core dump image in the core dump partition, read decrypted when the partition is encrypted |

## Writes and flash encryption

Every write starts at a multiple of 16 bytes and covers whole blocks. So the same layout works on a
partition with flash encryption.

On an encrypted partition, the device reads most bytes decrypted. Erased bytes do not read as
`0xFF` after decryption. So the device makes two tests on the bytes as stored, without decryption:

- whether a log sector's retire block is erased
- whether the first block of a record is erased

A decoder of an image of an encrypted partition needs both forms of the image: the raw image, and
the image decrypted with the device's flash encryption key. A decoder of an unencrypted partition
uses the same bytes for both tests and for all other reads.

## The store

The fault region is at the end of the store. The log region is the rest.

```
store: S sectors of sector_size bytes
+----------------------------------------------+------------------------------+
| log region: sectors 0 to L - 1               | fault region: F sectors      |
+----------------------------------------------+------------------------------+
F = slot_count * slot_size / sector_size
L = S - F
```

The sizes of the two regions are not written in the store. The device calculates them from the
values in this table. A decoder must be given the same values:

| Value | Where the device gets it | Default |
| ----- | ------------------------ | ------- |
| Sector size | The partition's erase size, or the passed store's sector size | 4096 on SPI flash |
| `slot_size` | `config::faults->slot_size` (`fault_config`), at run time | 8192 |
| `slot_count` | `config::faults->slot_count` (`fault_config`), at run time | 4 |
| Fault region present | `config::faults` is set (not `std::nullopt`) | Yes |

Without a fault region (`config::faults` is `std::nullopt`), the log region is the whole store. No
Kconfig option sets the size of a region.

The device checks these limits:

- `slot_size` is a multiple of the sector size, and more than 512 bytes.
- `slot_count` is at least 1, and `slot_size` is at most the store's size divided by `slot_count`.
- The fault region leaves at least one sector for the log region.
- The log region has at least 2 sectors.
- The sector size is at least 80 bytes, at most 65536 bytes, and a multiple of 16.

Slot `i` (from 0) starts at offset `i * slot_size` in the fault region. That is offset
`L * sector_size + i * slot_size` in the store.

## The log region

The log region is a sequence of `L` log sectors. Each log sector has its own header and holds
records. The device writes the log sectors in order, and it uses sector 0 again after sector
`L - 1`.

```
log region
+--------------+--------------+--------------+-----+------------------+
| log sector 0 | log sector 1 | log sector 2 | ... | log sector L - 1 |
+--------------+--------------+--------------+-----+------------------+
```

The subsections below describe one log sector, then the records in it, and then how the log
sectors together make the log.

### Log sector layout

One log sector has this layout:

```
offset 0             16             32                                    sector_size
+--------------------+--------------+----------+----------+----+------------------+
| sector header      | retire block | record   | record   | .. | erased           |
+--------------------+--------------+----------+----------+----+------------------+
```

The sector header (block 0):

| Offset | Size | Type | Field |
| ------ | ---- | ---- | ----- |
| 0 | 4 | ASCII | Magic, `PLOG` (`50 4C 4F 47`) |
| 4 | 2 | u16 | Layout version, 1 |
| 6 | 2 | u16 | Flags. Bit 0 is the start flag. The device writes the other bits as 0, and a decoder ignores them. |
| 8 | 4 | u32 | Sequence number |
| 12 | 4 | u32 | Sector header CRC, over bytes 0–11 |

The retire block (block 1, offsets 16–31) is erased while the sector is in use. Before the device
erases a sector that is in use, it writes 16 bytes of `0x00` to the retire block. The retire block
is one block because one block is the smallest write that the format permits (see
[Writes and flash encryption](#writes-and-flash-encryption)). It is a block of its own because the
header block is already written, and on an encrypted partition a block that is already written
cannot be written again without an erase.

The start flag marks the start of the log. The device sets it on the first sector that it writes
to an empty log region. The device also sets it on the sector that it starts for each clear of the
log.

The sequence number of each sector that the device starts is 1 more than the sequence number of
the sector that it started before. The first sector that the device writes to an empty log region
is sector 0, with sequence number 0. The device starts the sectors in order. Sector 0 follows
sector `L - 1`.

### Valid log sectors

A decoder makes these tests on each log sector, in this order:

1. The sector header has the magic `PLOG`, and its CRC matches. Otherwise the sector is unused.
   This is the result for an erased sector, for a damaged header, and for a header that was only
   partly written when power was cut.
2. The layout version is 1. A higher version is a newer layout. The sector is then unused, and a
   decoder can report that it found a newer layout. A sector with a lower version is unused, and a
   decoder does not report it.
3. The retire block, read as stored, is erased. Otherwise the sector is unused. The device was
   erasing it, and a power cut can have stopped the erase before it was complete.

A sector that passes all three tests is valid. The device checks the CRC before it reads the
version. So the device recognizes a newer layout only when that layout keeps the magic, the
version and the CRC at the offsets above.

### Record layout

Records start at offset 32. Each record starts on a block.

```
+0       +2         +6                         +6+length         next block
| length | CRC      | text (length bytes)      | 0xFF padding    |
```

| Offset | Size | Type | Field |
| ------ | ---- | ---- | ----- |
| 0 | 2 | u16 | Length of the text in bytes, at least 1 |
| 2 | 4 | u32 | Record CRC, over the 2 length bytes and then the text |
| 6 | length | bytes | Text |
| 6 + length | to the next block | bytes | Padding, `0xFF` |

A record uses `6 + length` bytes, rounded up to a multiple of 16. The next record starts
immediately after it. A record is always in one sector. The largest length of a record that starts
at offset `o` is `sector_size - o - 6`.

A decoder reads the records of a sector from offset 32. At each offset `o`:

1. If the block at `o`, read as stored, is erased, the block is a blank block. The sector's
   records end here.
2. Otherwise, read the length. If it is 0, or greater than `sector_size - o - 6`, the record is
   bad.
3. Compute the record CRC. If it doesn't match the stored CRC, the record is bad.
4. Otherwise the record is good. Add its text to the text stream. Continue at the next record
   while `o` is less than the sector size.

A bad record ends the sector's records. The device never writes after a bad record. A power cut
during a record's write leaves a bad record, or leaves the record's first block erased.

### Finding the log

1. Test every log sector, as described in [Valid log sectors](#valid-log-sectors).
2. The head is the valid sector with the highest sequence number. Compare the numbers as
   unsigned integers. If more than one valid sector has that number, the head is the one with the
   lowest sector index. If no sector is valid, the log is empty.
3. The log starts as the head only. Go backward from the head, one sector at a time. The sector
   before sector `s` is `(s + L - 1) % L`. Add that sector to the log if it is valid and its
   sequence number is 1 less than the sequence number of the sector after it. Otherwise stop. Stop
   also after you add a sector that has the start flag, and when the log holds all `L` sectors. If
   the head has the start flag, the log is the head only.
4. The last sector that you added is the oldest sector. Read the sectors from the oldest sector to
   the head.

Sectors that are not in the log can still hold text, for example the text from before a clear. The
device never returns that text.

### Building the text stream

The text stream is the text of the good records, in order. The positions of the record and
sector boundaries have no meaning. The device splits text where the text doesn't fit in a sector,
and one record can hold many log entries. Apply these rules while you join the text:

- **Oldest sector without the start flag.** The log region has wrapped, so the sector may begin in
  the middle of a log entry. Drop the text up to and including the first `\n`. Then drop each line
  that starts with a tab (`\t`).
- **Blank block.** The sector's records end. The text continues in the next sector. Add nothing
  between the two sectors' text.
- **Bad record.** The sector's records end. If text has been kept and it doesn't end with `\n`, add
  a `\n`. Take the first byte of the next sector's text as the start of a line, and drop each line
  from there that starts with a tab.

Because of these rules, the text stream always begins at the start of a log entry.

### Wrap and clear

When the device needs a new sector, it uses the sector after the head. If that sector is valid,
the device first writes the retire block. It then erases the sector and writes the sector header.
When every log sector is in the log, the new sector overwrites the oldest sector, and the text of
the oldest sector is lost.

A clear of the log starts a new sector with the start flag. It does not erase the older sectors.
The device reads the log from the newest sector that has the start flag.

### Write order and power cuts

The device writes in this order:

1. To start a sector: the retire block (only if the sector is valid), the erase, then the sector
   header.
2. To write a record: the first block (length, CRC and up to 10 bytes of text), then the whole
   blocks of the rest of the text, then the last block padded with `0xFF`.

A power cut has these effects:

| Power cut during | Result |
| ---------------- | ------ |
| The retire block write | The retire block isn't erased. The sector is unused. |
| The erase | The sector is unused, because its retire block isn't erased or its header isn't valid. A reader does not return its old text. |
| The sector header write | The header CRC fails. The sector is unused. The device starts the same sector again with the same sequence number. |
| A record write | The record is bad, or its first block is erased. Only that record is lost. The device writes the next record in a new sector. |

When the device opens the log, it scans the head sector. If any byte after the last good record
isn't erased, the next write starts a new sector. If the text stream doesn't end with `\n` and the
log doesn't end at a bad record, the device writes a record that holds only `\n`.

## Log entries

A log entry is a stamp, the entry's text, and a newline:

```
<boot>.<wake> +<uptime_ms> <wall> <first line>\n\t<second line>\n\t<third line>\n
```

| Stamp field | Form | Meaning |
| ----------- | ---- | ------- |
| `<boot>` | Decimal, u32 | The boot count that the app passes in `config::boot` |
| `<wake>` | Decimal, u32 | The number of wakes from deep sleep since the last reset of another kind |
| `<uptime_ms>` | Decimal, after `+` | Milliseconds since the boot or the deep-sleep wake |
| `<wall>` | `YYYY-MM-DDTHH:MM:SSZ`, or `-` | The wall-clock time in UTC, to the second. `-` when the clock isn't set or the year is outside 0 to 9999. |

A single space follows each of `<boot>.<wake>`, `+<uptime_ms>` and `<wall>`. The stamp is at most
65 bytes. The device's parser accepts a stamp of at most 72 bytes (`max_prefix_size`).

Each later line of an entry starts with a tab. The device drops empty lines from an entry's text.
So within an entry, every `\n` except the last is followed by `\t`. To get the original text,
remove the stamp and replace each `\n\t` with `\n`.

### Entry text

The device writes these forms of entry text:

| Source | Text |
| ------ | ---- |
| A captured log line | `<L> <tag>: <message>`, or `<L> <message>` when the tag is empty |
| `persistent_log::append()` | The text of the call, without leading newlines |
| Entries that the component adds itself | For example `E persistent_log: fault kind=<kind> <detail>`, `W persistent_log: dropped <n> bytes`, `I persistent_log: reset reason=<name>` |

`<L>` is the level letter: `E` (error), `W` (warning), `I` (info), `D` (debug), `V` (verbose), or
`?` for any other level. The entry's text, without its stamp and its tabs, is at most
`CONFIG_IDFXX_PERSISTENT_LOG_MAX_LINE` bytes (default 256). The device cuts a captured line's tag to
half of that size. The device cuts text only at the start of a UTF-8 character. The device does
not mark an entry that it cut, and it does not count the bytes that it cut in a `dropped` entry.

A `W persistent_log: dropped <n> bytes` entry means that `<n>` bytes of log text were lost before
they reached flash. The device does not check the bytes of an entry's text. A `\n` in the text
always ends a line.

### Splitting the text stream into entries

1. Skip any `\n` bytes at the current position.
2. The entry ends at the first `\n` that is not followed by `\t`, or at the end of the text. The
   entry's text does not include that final `\n`.
3. Parse the stamp from the start of the entry: digits, `.`, digits, a space, `+`, digits, a space,
   the wall time or `-`, a space. Each number must fit its type. The wall time must be a valid date
   with hours 0–23, minutes 0–59 and seconds 0–59. The stamp must be at most 72 bytes. If any part
   fails, the entry has no stamp, and all of its bytes are its text.

## The fault region

The fault region is a sequence of `slot_count` slots. Each slot is `slot_size` bytes, and it holds
one fault entry or is empty. The device writes the slots in order, and it uses slot 0 again after
the last slot.

```
fault region
+--------+--------+--------+-----+---------------------+
| slot 0 | slot 1 | slot 2 | ... | slot slot_count - 1 |
+--------+--------+--------+-----+---------------------+
```

The subsections below describe one slot, and then how the slots together make the list of faults.

### Slot layout

One slot has this layout:

```
slot offset 0                   512                         512 + context size      slot_size
+-------------------------------+---------------------------+-------------+---------------+
| slot header                   | context                   | 0xFF pad    | not used      |
+-------------------------------+---------------------------+-------------+---------------+
```

The context starts at offset 512. The device pads it with `0xFF` to a whole block. The bytes after
the padding are not part of the entry. They can hold an older entry's context.

### Slot header fields

| Offset | Size | Type | Field |
| ------ | ---- | ---- | ----- |
| 0 | 4 | ASCII | Magic, `PFLT` (`50 46 4C 54`) |
| 4 | 2 | u16 | Layout version, 1 |
| 6 | 2 | — | Padding, 0 |
| 8 | 4 | u32 | Sequence number |
| 12 | 4 | u32 | Boot count |
| 16 | 4 | u32 | Wake count within the boot |
| 20 | 4 | — | Padding, 0 |
| 24 | 8 | i64 | Wall-clock time, in seconds since 1970-01-01T00:00:00Z. `INT64_MIN` (`0x8000000000000000`) when the clock wasn't set. |
| 32 | 4 | u32 | Milliseconds since the boot or the deep-sleep wake. Values above `0xFFFFFFFF` are stored as `0xFFFFFFFF`. |
| 36 | 4 | u32 | Context size in bytes |
| 40 | 4 | u32 | Context CRC |
| 44 | 24 | string | Kind |
| 68 | 32 | string | Firmware version |
| 100 | 160 | string | Detail |
| 260 | 8 | bytes | The first 8 bytes of the SHA-256 of the app's ELF file. All 0 when not known. |
| 268 | 16 | string | Name of the task that the fault happened in. Empty when not known. |
| 284 | 4 | u32 | Program counter where the exception happened, or 0 when not known |
| 288 | 4 | u32 | Frame count, 0 to 16 |
| 292 | 64 | u32[16] | Backtrace frames, as program counters, innermost first. Frames past the frame count are 0. |
| 356 | 4 | u32 | Core dump CRC flag: 1 when offset 360 holds a CRC, otherwise 0. The device reads any value other than 1 as 0. |
| 360 | 4 | u32 | Core dump CRC (see [Core dump CRC](#core-dump-crc)), or 0 when the flag is 0 |
| 364 | 144 | — | Reserved, 0 |
| 508 | 4 | u32 | Slot header CRC, over bytes 0–507 |

The device writes padding and reserved bytes as 0. A decoder ignores them.

### String fields

A string field holds text followed by a NUL byte. The bytes after the NUL are 0. The text is at
most the field size minus 1 bytes: 23 for the kind, 31 for the version, 159 for the detail and 15
for the task name. The device cuts longer text to fit, and the cut does not end in the middle of a
UTF-8 character. A decoder takes the text up to the first NUL, and treats the field's last byte as
NUL.

The kind is 1 to 23 printable ASCII characters (`0x21` to `0x7E`), with no spaces. The kind
`cleared` is reserved for a cleared entry. The other strings can hold any bytes, and are normally
UTF-8.

### Context

The context is log text, in the format of [Log entries](#log-entries). It begins at the start of a
log entry. It holds the newest complete log entries that fit in the slot. For a fault that the
device records, the context normally ends with the fault's own log entry
(`E persistent_log: fault kind=<kind> <detail>`).

The context size is at most `slot_size - 512`. The context CRC covers exactly the context size in
bytes.

### Core dump CRC

The value at offset 360 is the CRC of the last core dump image that a recorded fault took details
from. The image is the core dump that ESP-IDF reports in the core dump partition, read decrypted
when the partition is encrypted. The CRC covers the whole image.

Each entry sets the two fields as follows:

- A fault that took details from a core dump writes that dump's CRC, and sets the flag to 1.
- Every other entry, including a cleared entry, copies the flag and the CRC from the entry before
  it. The entry before it is the entry with the highest sequence number, even when a clear hides
  that entry.
- When the fault region holds no entry, an entry that took no details from a core dump writes the
  flag and the CRC as 0.

So the newest entry holds the CRC of the last core dump that a recorded fault took details from.
This is true whether a clear hides the newest entry or not.

After a panic, an interrupt watchdog reset or a task watchdog reset, the device compares the CRC of
the core dump in the core dump partition with this value. When they match, the dump belongs to a
fault that is already recorded, and the device does not take details from it again. The device also
compares the dump's CRC with the CRC in RTC memory (see [RTC memory](#rtc-memory)). The value in
flash prevents the use of an old core dump for a later fault after RTC memory is lost, for example
after a power cut. A fault that is suppressed as a repeat writes no entry. If only a suppressed
fault took details from a dump, only RTC memory holds that dump's CRC.

### Valid fault entries

A decoder makes these tests on each slot header, in this order:

1. The slot header has the magic `PFLT`, and its CRC matches. Otherwise the slot is empty. This is
   the result for an erased slot, for a damaged header, and for a header that was only partly
   written when power was cut.
2. The layout version is 1. A higher version is a newer layout. The slot is then empty, and a
   decoder can report that it found a newer layout. A slot with a lower version is empty, and a
   decoder does not report it.

A slot that passes both tests holds a fault entry. The device checks the CRC before it reads the
version. So the device recognizes a newer layout only when that layout keeps the magic, the version
and the CRC at the offsets above.

A fault entry is damaged when either of these is true:

- The context CRC does not match. The entry is still listed.
- The context size is greater than `slot_size - 512`. This happens when the slot size used to read
  is not the slot size used to write. The device reads only `slot_size - 512` bytes of context,
  and a decoder does the same.

A decoder reads at most 16 frames, even when the frame count is greater than 16.

### Finding and listing fault entries

1. Read every slot header, and keep the slots that hold a fault entry.
2. The newest entry is the entry with the highest sequence number. Compare the numbers as
   unsigned integers. If no slot holds an entry, the store holds no faults.
3. The floor is the highest sequence number of any cleared entry. There is no floor when no slot
   holds a cleared entry.
4. Go through all the slots in order. Start at the slot after the newest entry. Slot 0 follows
   the last slot. List each fault entry whose sequence number is greater than the floor. This
   order lists the oldest entry first.

The listing never includes a cleared entry, because the newest cleared entry's sequence number is
the floor. The device writes entries in slot order and never skips a slot. So slot order from the
slot after the newest entry is the order in which the entries were recorded.

### Recording and clearing

The next entry goes in the slot after the newest entry, with the next sequence number. In an empty
fault region, the first entry goes in slot 0 with sequence number 0. When every slot holds an
entry, the next entry overwrites the oldest entry.

A clear writes a cleared entry. The cleared entry has kind `cleared` and no context. Its boot
count, wake count, uptime, exception program counter and frame count are 0. Its wall time is
`INT64_MIN`. Its version, detail and task name are empty, and its ELF SHA-256 field is 0. Its core
dump fields are copied from the entry before it. A clear hides every entry with a sequence number
at or below its own. This is still true after the cleared entry is overwritten. The cleared
entry was the oldest entry at that time, so every older entry was already overwritten.

### Write order and power cuts

The device writes a fault entry in this order:

1. Erase the sectors that the entry needs, from the slot's first sector. That is
   `ceil((512 + context size rounded up to a block) / sector_size)` sectors. The sector that holds
   the slot header is erased first.
2. Write the context, in pieces of up to 256 bytes. Pad the last piece with `0xFF` to a whole
   block.
3. Write the slot header, one block at a time, from offset 0. The block at offset 496 holds the
   slot header CRC.

A power cut before the whole slot header is written leaves a slot header whose CRC fails. The slot
is then empty, and the next entry goes in the same slot with the same sequence number. The entry
that the slot held before is lost.

With one slot (`slot_count` of 1), the slot that the device overwrites holds the newest entry. A
power cut then leaves no entry. The next entry then takes sequence number 0, and the core dump
fields of the lost entry are not carried forward.

## Handling damaged and unknown data

- Test each sector header and slot header with its magic and CRC before any other field.
- Treat a log sector or a slot of a newer layout version as unused. Do not decode its other
  fields.
- In the log, a bad record ends its sector. Continue at the next sector of the log, as
  [Building the text stream](#building-the-text-stream) describes.
- List a fault entry whose context fails its CRC, and mark it as damaged.
- Never read a context past its context size.
- Ignore padding and reserved bytes.

## RTC memory

This section describes RAM, not flash. RTC memory keeps its contents through deep sleep and
through every reset except a power cut. After power-on, it holds random bytes. The linker sets the
addresses of these structures, so the addresses depend on the build. All fields other than the
ring's `data` are `u32`, little-endian.

The ring holds log text that is waiting to be written to flash. Its size `N` is
`CONFIG_IDFXX_PERSISTENT_LOG_RTC_RING_SIZE`. It has no CRC.

| Field | Meaning |
| ----- | ------- |
| `magic` | `0x32524C4D` (`MLR2`) XORed with `N` |
| `begin` | Index of the oldest valid byte |
| `flushed` | Index of the oldest byte not yet written to flash |
| `head` | Index one past the newest byte |
| `dropped` | Number of bytes lost before they were written to flash |
| `dropped_at_flush` | The value of `dropped` that the last flush reported. `dropped` minus this value is the number of lost bytes that no flush has reported. |
| `data` | `N` bytes. The byte with index `i` is at `data[i % N]`. |

The indices count modulo the largest multiple of `N` that is at most 2^32.

The other state is in three RTC records. An RTC record is not a record of the log. Each RTC
record ends with a CRC-32 (as in [Byte order and CRC](#byte-order-and-crc)) of the words before it.
An RTC record with a wrong magic, a wrong CRC or a flag greater than 1 is not valid. The device
sets such an RTC record to zeros, and then writes its magic and its CRC.

| RTC record | Magic | Fields, in order |
| ------ | ----- | ---------------- |
| Hint | `0x31484C50` (`PLH1`) | `magic`, `has_hint` (0 or 1), `sector_size`, `size` (the log region's size in bytes), `sector`, `offset`, `seq`, `crc` |
| Wake | `0x31574C50` (`PLW1`) | `magic`, `wake` (the wake count), `crc` |
| Fault | `0x31464C50` (`PLF1`) | `magic`, `has_core_dump_crc` (0 or 1), `core_dump_crc`, the suppressed fault kinds, `crc` |

The hint holds the position of the log's writer: the head sector, the offset of the next record
in that sector (the sector size when the next record goes in a new sector), and the head's sequence
number. The
device uses it only for a log region of the same sector size and size, and only when the flash
still matches it.

The suppressed fault kinds are `CONFIG_IDFXX_PERSISTENT_LOG_SUPPRESSED_FAULTS` pairs of
`kind` and `order`, then `next_order`. `kind` is the CRC-32 of the kind text (1 when that CRC is 0),
or 0 for an empty pair. `order` is the value that `next_order` had when the kind was last
recorded. `next_order` increases by 1 each time.
