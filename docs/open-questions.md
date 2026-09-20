# Open questions

Two kinds of thing are listed here. The first is what the datasheet itself
leaves unresolved, gathered from section 13 and section 16 so it is in one
place. The second is what this implementation had to decide because the
datasheet does not say, which is the more dangerous kind: those are choices we
made, marked ASSUMPTION in the source, and any of them could be wrong.

A logic analyzer on a live chain settles most of this in an afternoon. Reading
registers back off silicon settles the rest. Until then it stays on this list.

## Ours to answer

### Byte order of the nonce start, the timestamp and the nonce ceiling

`src/ic_work.c` writes all three little-endian. The datasheet fixes the byte
order of the target words and of the nonce as it comes back in a result, but
not of these three. Little-endian is what a plain 64-bit store on the ARM
controller produces, and it is what the result path implies for the nonce, so
it is the most likely reading. It is still a reading.

**Settles it:** capture one work load and compare the bytes at offsets 0x20 and
0x28 against the header the pool sent.

### The all-zero reply to a work load

Section 7.4 says the expected reply is an all-zero frame of the same length.
There is no preamble in an all-zero frame, so `ic_xact` checks that the bytes
the chain clocked back are quiet. An idle MISO line and a chain that answered
with zeros are identical to the host, so this check confirms almost nothing. If
the chain really does drive something, a work load that silently fails looks
exactly like one that worked.

**Settles it:** capture MISO during a work load.

### Where the reply sits in the transfer

The turnaround rule produces a transfer of `6a + max(frame_len, reply_len)`,
which puts the reply in the window starting at the hop latency and ending
exactly at the end of the transfer. `tests/fake_chain.c` places it there and
`ic_frame_find` scans for it, so the library does not depend on the derivation.
A controller that assumed the reply arrives after the request frame would run
off the end of its buffer on a long chain.

**Settles it:** capture one addressed read at a high chip address.

### The broadcast read inside the PLL sequence

Setting a frequency is a read-modify-write of register 0, and bring-up does it
broadcast. A broadcast read has no single answer. `ic_set_freq` issues the read
at whatever address it was given, which is what the controller does, and takes
whichever reply arrives. Every chip on a chain is presumably in the same state
at that point in bring-up, so the bits outside the mask are presumably
identical, but presumably is doing a lot of work in that sentence.

**Settles it:** read register 0 from several chips individually and compare.

### Whether 04NN echoes its operand

Table 7-1 gives the work configuration reply one payload packet and does not
say what is in it. The simulator echoes the operand. Nothing in the library
depends on the contents.

### ICC551 header length

`ic_icc551.header_bytes` is 80, inferred from a 96-byte payload minus the
16-byte trailer, which is exactly ICT580. Cores per chip and the nonce ceiling
are build constants that exist nowhere for this part, so they are zero and
`ic_work_build` refuses to build for it rather than guess a ceiling.

### Retry counts

The retry column of table 7-1 is taken as the number of retries after the first
attempt, so a poll makes eleven attempts. If it meant total attempts, every
command here tries one more time than the controller does.

## Settled against the live chain

Not open questions any more, but the kind of thing that bites twice if it is
not written down.

### The library reproduces every block since the fork

`tools/chain-headers.sh | build/ic-verify-block -` rebuilds the buffer a mining
chip hashed for each block and compares the result to the hash the chain gives
it. As of height 973007 that is 11,368 blocks since the fork at 961640, all
matching, including the blocks that set an XOR key and the blocks that use a
time offset. That is agreement with the chain rather than with test data.

### Blob byte order differs between the two sources we read

The Knots test vector file `src/test/data/block_header_v2.json` stores blob
fields in display order, the reverse of the wire. The node's REST interface
stores `xor_key` in wire order. They disagree, and both are internally
consistent, so code that reads one and is then pointed at the other will build
a wrong mask and a wrong hash without any obvious symptom.

`tools/gen-vectors.c` reads the vector file and reverses. `ic-verify-block`
reads raw serialized headers and does not. Anything new that consumes header
fields from a JSON source should establish which convention it is looking at
before trusting a blob.

### Nobody is mining any profile but 0

Every one of those 11,368 blocks carries `m_flags & 3 == 0`, which is the
Siacoin layout that ICT580 and ICA586 hash unchanged. Not one block has been
mined with profile 1, 2 or 3. 584 of them set an XOR key, so some pools are
hiding the target from their miners, and 767 use a time offset.

That is the case for building profile 1 silicon stated as a measurement: the
entire network is one part family, and the profile that was left for new
entrants has never been used.

## Knots work, ours to answer

### How the chip rolls the timestamp

A result reports a timestamp index, not the timestamp it used. Section 10 says
only "which rolled timestamp the result is against". Nothing says how many the
chip tries or what it does to the eight bytes at 0x28 between them.

This blocks reconstructing the header from a result: on profile 0 those eight
bytes are m_time_offset and m_nonce3, so without the index-to-bytes rule the
host does not know what the chip actually hashed. The workaround is cheap,
since the index is one byte: try all 256, hash each, keep the one that matches
the reported nonce. A controller can ship that way and the real rule only makes
it faster.

**Settles it:** capture one work load and the result that came back, or read
the index handling out of the controller binary.

### The ICA586 target window

Writing 0x13 and 0xDF over the top two bytes of the target bound leaves a bound
whose top 16 bits are fixed. Read as a plain 64-bit comparison that is a target
far easier than difficulty 1, which cannot be what the part does. The datasheet
reaches the same conclusion from the other direction and says the overwrite
implies the chip compares a limited range and the upper bits carry something
else.

Until that is settled we do not know what difficulty can be asked of an ICA586,
which matters because SC5 Pro II is the one product built on it.

**Settles it:** set a known target on a live ICA586 and measure the rate of
reported results against the rate the bound predicts.

### Which part is in an SC5 Pro

The product table covers SC5 Pro II, which is ICA586. Plain SC5 Pro appears
nowhere in the datasheet. Extracting the stock firmware per appendix A and
reading /root/product.json answers it without touching the miner.

## The datasheet's own unknowns

### Register 4, bits [31:28] (section 13.1)

Two values across every product examined, `0x800003F1` and `0x700003F1`, with
four correlations that separate the sample identically and cannot be told apart
from these twelve products. `ic_chain_start_value` implements the index-budget
hypothesis because it reproduces every observation, and `tests/test_result.c`
pins it against table 13-1. It is a hypothesis. Prefer a value read off the
product you are replacing when you have one.

The prediction worth testing: a 128-chip chain still reads `7`, and the first
`6` appears at 129 chips. Nothing in this family goes past 96.

### The 0x00E5 operand (section 13.2)

Broadcast before every work load, on every product, as a compile-time constant
with no configuration path. `IC_WORKCFG_OPERAND` is that constant. No field
structure can be inferred from usage, so changing it is an experiment with no
prediction attached.

### Command 0C00 (section 13.3)

Defined in all four command tables, issued by nothing, five packets out and
fifteen plus a CRC back. It is in the command table here with its shape and no
helper, because sending an unknown command to an unknown device is the caller's
decision.

### The thermal lookup table (section 12)

156 monotonic entries compiled into the controller, not published. Only the two
endpoints are: raw 2804 is 125 C, raw 3420 is -30 C, and a code past the end of
the table yields -35. `ic_temp_from_raw` interpolates between the anchors and
`ic_temp_set_table` installs the real table once someone lifts it.

This is the single highest-value thing to extract from a firmware image. Every
thermal decision a replacement controller makes runs through it.

### Registers 1, 2, 5, above 7, and 0xFFFF

Untouched by the controller. Their existence is neither confirmed nor excluded.
The identify register has a read helper in the controller that no product
calls, and its response format is unknown.

### Voltage control (section 13.4)

Each control board declares an ISL8118 regulator over three GPIOs, and the
mining controller's voltage setter stores the value and drives nothing. Either
something else in the system programs the rail or it is fixed in hardware.
There is no voltage hook in `struct ic_board` for that reason.

### Nonce partitioning (section 13.5)

Every chip gets the same broadcast work item and the same starting nonce, and
results come back tagged with chip and core. How the chain divides the search
space is internal, and nothing the host sends influences it.

## Where a discrepancy is most likely

Section 16 names the two places: fields the controller writes blindly and never
reads back, which are register 4 and the `0x00E5` operand, and behavior the
controller never exercises, which is registers 1, 2 and 5, command `0C00`, and
whatever the chain does when a command arrives out of the expected order.
