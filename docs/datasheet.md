# Intchains ASIC Datasheet

**ICT580, ICC590, ICA586, ICC551: host interface specification, rev 0.2**

The SPI command, register and work-payload interface of the Intchains Blake2B
mining ASICs used in Goldshell miners. Written as an implementation target:
everything a replacement controller has to emit, and everything the chain emits
back.

| | |
|---|---|
| **Method** | static analysis, Goldshell `intminer` 2.2.x |
| **Evidence** | 63 firmware images, 12 products, 3 bound chip families |
| **Silicon** | not consulted |
| **Bus capture** | none |

## Credit

**This document is not mine.** The reverse engineering behind it, and the
specification itself, are Luke Dashjr's. He published it at:

<https://luke.dashjr.org/tmp/code/intchains-asic-datasheet.html>

and posted that link in the #mining channel of the Bitcoin Knots Discord on
2026-09-07. The page carries no byline; the attribution here is from him
posting it himself.

The original HTML is kept beside this file as `datasheet-rev0.2.source.html`,
byte for byte as it was served, so the transcription below can be checked
against it. Everything of value in sections 1 through 16 and the appendix is
his work. What I did was change the markup.

This is a transcription, not a rewrite. The wording, the numbers and the status
marks are the source document's. What changed: HTML became markdown and the two
SVG figures became ASCII drawings; em dashes became ordinary punctuation; the
two British spellings became American ones; and every other character outside
plain ASCII was replaced by its ASCII equivalent, so the section sign became
the word "section", the degree sign was dropped, and the arrows carrying the
work payload table became "same" and "cont.". This file is 7-bit ASCII
throughout. Nothing else was touched, and the unedited original is beside it.

## Status legend

| Mark | Meaning |
|---|---|
| **V** | **Verified.** Read from executed code paths and consistent across every image examined. |
| **O** | **Observed.** Read from one site or one product; correct as far as it goes, narrow evidence. |
| **S** | **Speculative.** Inference or hypothesis. No direct evidence. Flagged inline, never mixed into normative text. |

---

## 1 Overview

Each device is a Blake2B search engine with an on-chip PLL, a temperature
sensor, and a serial port that daisy-chains to its neighbors. A controller
board (CPB) drives one chain from a single SPI master port. Chips self-assign
addresses at power-up; thereafter the host addresses them individually or
broadcasts to all.

The host loads an identical work item to every chip on a chain, then polls for
results. Work partitioning happens inside the chain; the host never assigns
nonce ranges. Each result identifies the chip and core that produced it and the
work item it belongs to.

```
                                 CONTROL & POWER BOARD (CPB) - 1 to 4 per miner
                            +--------------------------------------------------------+
  +--------------+   SCLK   |  +----------+    +----------+           +-----------+   |
  |   HOST SoC   |----------|  |  CHIP 1  |----|  CHIP 2  |-- ... ----|   CHIP N  |   |
  | ARM Linux 3.10  MOSI    |  | addr 0x01|    | addr 0x02|           | N = 16..96|   |
  | /dev/spidevN.M |--------|  +----------+    +----------+           +-----------+   |
  +--------------+   MISO   |                                                         |
                            |  RESET / ENABLE / PLUGIN (GPIO)                          |
                            |  ISL8118 rail, TMP112 x1-2                              |
                            |  host-driven, not on the chain                           |
                            +--------------------------------------------------------+

  Reply latency grows 6 bytes per chip hop - see section 6.
```

One SPI master per CPB. A miner has one to four CPBs; the protocol below is
per-chain and identical on all of them.

---

## 2 Device family

Four back-ends exist in the controller software. Three are bound in shipping
products; `ICC551` is compiled in but no product reaches it.

**Table 2-1. Family parameters**

| Part | Cores/chip | Work payload | Nonce ceiling | SPI | Seen in | Status |
|---|---|---|---|---|---|---|
| **ICT580** | 80 | 48 pkt, 96 B | `0x0000FFFFFFFFFFFF` | 500 kHz, mode 1 | SCBox, SCBox II, SC6-SE | V |
| **ICC590** | 20 | 72 pkt, 144 B | `0x0000FFFFFFFFFFFF` | 500 kHz, mode 1 | HS3, HS3-SE, HS5, HS6, HS6-SE, HS-BOX, HS-BOX II, HS-LITE | V |
| **ICA586** | 20 | 56 pkt, 112 B | `0x00003FFFFFFFFFFF` | 500 kHz, mode 1 | SC5 Pro II | V |
| **ICC551** | - <sup>a</sup> | 48 pkt, 96 B | - <sup>a</sup> | 500 kHz, mode 1 | none, compiled in but never bound | O |
| **ICA590** | Not a back-end. A configuration name only <sup>b</sup> | | | | HS-BOX II (runs ICC590) | V |

<sup>a</sup> Cores per chip and the nonce ceiling are properties of the *build*,
not of the back-end: both are compile-time constants in the controller rather
than fields of the chip driver. Because no product binds ICC551, neither value
exists anywhere to be read. Its payload length, SPI rate, PLL mask and command
table are back-end properties and are recorded above.

<sup>b</sup> The chip type is fixed at build time and the `chips.model` string in
the configuration file is never compared against anything; it is documentation,
not selection. HS-BOX II declares `ICA590`, a name with no corresponding
back-end, and runs ICC590. Treat configuration model strings as unreliable.

Everything in sections 3 through 12 is common to all parts except the work
payload length (section 9) and the nonce ceiling. Verified as identical across
all four back-ends: the frame grammar and complete command table (byte-identical
apart from the work packet count), the 500 kHz SPI rate and mode, the PLL mask
`0xFE00FC8F`, and the PLL divider table. A fifth mask, `0xFE00FC0D`, exists in
an unbound generic helper and applies to none of these parts.

---

## 3 Host interface

**Table 3-1. SPI master configuration**

| Parameter | Value | Notes | Status |
|---|---|---|---|
| Clock rate | 500 000 Hz | Identical in all four back-ends. No product overrides it. | V |
| Mode | 1 (CPOL 0, CPHA 1) | Sampled on the falling edge. | V |
| Word size | 8 bits | Frames are built as byte streams; 16-bit structure is logical. | V |
| Bit order | MSB first | Standard spidev default; never overridden. | V |
| Transaction | 1 full-duplex transfer | One `SPI_IOC_MESSAGE(1)` carries request, turnaround and reply. | V |
| Max transfer | 262 143 B | Host-side buffer limit, not a device limit. | O |

```c
/* Bring the port up exactly as the controller does. */
fd = open(spi_path, O_RDWR);                            /* one device per CPB */
ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &(uint32_t){500000});
ioctl(fd, SPI_IOC_WR_MODE,          &(uint8_t) {1});
ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &(uint8_t) {8});
```

There is no separate read transaction and no interrupt or ready line. A command
is one transfer whose transmit buffer holds the request frame followed by enough
filler bytes to clock the reply back out of the chain (section 6). The
controller then searches the receive buffer for the expected reply.

---

## 4 Frame format

The unit of framing is a 16-bit **packet**, transmitted most-significant byte
first. A frame is five fields, two of which are always present.

```
  MOSI: HOST -> CHAIN
  +---------+---------+--------------------------+---------+---------+//////////////////////////+
  |  A5 3C  |   cmd   |  payload, 0..72 packets  |  CRC16  |  00 00  | filler 0x00 x (6*addr,   |
  | preamble|  Y O NN | omitted when the command | optional|  tail   | or 900 broadcast) keeps  |
  |         |         | takes none               |         |         | SCLK running while the   |
  +---------+---------+--------------------------+---------+---------+ reply propagates back    |
  |<------- CRC covers preamble + cmd + payload ----------->|         //////////////////////////+

  MISO: CHAIN -> HOST
  +--------------------------+---------+---------+------------------+---------+---------+
  | undefined until the chain|  A5 3C  |   cmd   |     payload      |  CRC16  |  00 00  |
  | answers                  |         | echo or |                  |         |         |
  |                          |         | tagged  |                  |         |         |
  +--------------------------+---------+---------+------------------+---------+---------+
```

The reply is not at a fixed offset. The controller searches the receive buffer
for the preamble, then matches the command word against the templates for the
command in flight.

**Table 4-1. Frame fields, in transmission order**

| Field | Size | Value | Present |
|---|---|---|---|
| Preamble | 1 pkt | `0xA53C` | Always |
| Command | 1 pkt | `0xYONN`, see section 4.1 | Always |
| Payload | 0-72 pkt | Command-specific (section 7) | Per command |
| CRC | 1 pkt | CRC-16/KERMIT over preamble + command + payload (section 5) | Per command |
| Tail | 1 pkt | `0x0000` | Always |

### 4.1 Command word

Every command word is `0xYONN`:

- **NN**: bits [7:0], chip address. `0x00` broadcasts to the chain;
  `0x01`...`N` address one chip. Addresses are assigned by the chain during
  auto-addressing (section 7.2), not by the host. The address is a full byte on
  every product regardless of chain length.
- **O**: bits [11:8], opcode.
- **Y**: bits [15:12], tag. Zero on ordinary host requests. On *load work* it
  carries the 4-bit work ID, and the chip returns that same ID with any result
  for that work. On a register read the chip replies with `Y = 1`.

The controller matches replies against wildcard templates in which `N`, `X` and
`Y` are don't-care characters: a reply to `0x0800` matches either `0800` (an
echo) or `Y8NN` (a result). A reply matching no template for the command in
flight is discarded and the transaction retried.

---

## 5 Checksum

CRC-16/KERMIT: reflected CRC-CCITT, polynomial `0x1021` reflected to `0x8408`,
initial value `0x0000`, no final XOR. It covers the preamble, command and
payload; the tail is excluded. The bytes of each 16-bit packet are swapped
before the CRC runs, which is the same as running it over the packet values
serialized little-endian. The 16-bit result is transmitted MSB first like every
other packet.

```c
uint16_t ic_crc16(const uint8_t *wire, size_t len)   /* len MUST be even */
{
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++)
        crc = kermit_tab[(crc ^ wire[i ^ 1]) & 0xff] ^ (crc >> 8);
    return crc;                       /* transmit high byte first */
}
```

An odd length is a hard error in the reference implementation, which returns
`0xFFFF`, a value no real checksum can match, so the frame is retried until the
retry budget is exhausted.

---

## 6 Bus timing

The chain is a daisy chain and a reply propagates back through every chip
between the target and the host. There is no flow control; the host must keep
the clock running. After the request frame the host appends filler bytes of
`0x00`.

**Table 6-1. Turnaround, in filler bytes appended after the frame**

| Case | Filler bytes | Notes | Status |
|---|---|---|---|
| Addressed, chip *a* | 6 x a | Six bytes of latency per chip hop. | V |
| Broadcast (address 0) | 900 | Fixed; covers the longest chain in the family (96 x 6 = 576). | V |
| Longer reply than request | + (reply_len - frame_len) | Added when positive, on top of the above. | V |

This is the most load-bearing timing rule in the protocol. A controller that
clocks too few bytes never sees the reply and will read the chain as dead.

---

## 7 Command reference

**Table 7-1. Command summary. Packet counts are payload only.**

| Cmd | Name | TX pkt | Reply cmd | RX pkt | Retries | Status |
|---|---|---|---|---|---|---|
| `01NN` | Self-test, phase 1 | - | echo | - | 0 | V |
| `0200` | Auto-address / enumerate | 1 | echo | 1 | 1 | V |
| `03NN` | Self-test, phase 3 | - | echo | - | 0 | V |
| `04NN` | Work configuration | 1 | `04NN` | 1 | 1 | O |
| `Y7NN` | Load work | 48/56/72 + CRC | zeros | 48/56/72 | 5 | V |
| `0800` | Poll result | - | echo *or* `Y8NN` | 5 + CRC | 10 | V |
| `09NN` | Write register | 3 + CRC | `09NN` | 3 | 5 | V |
| `0ANN` | Read register | 1 | `1ANN` | 3 + CRC | 5 | V |
| `0BNN` | Self-test, phase 2 | - | echo | - | 0 | V |
| `0C00` | Unknown | 5 | `0C00` | 15 + CRC | - | S |

### 7.1 Self-test: 01NN, 0BNN, 03NN

Always issued as a broadcast triple in the order `01` -> `0B` -> `03`, with no
payload and no retries. Any non-zero return from any of the three is treated as
a single self-test failure. Their individual meanings are not distinguished by
the controller.

### 7.2 Auto-address: 0200

Broadcast with one payload packet of `0x0000`. The 16-bit reply payload is the
**number of chips on the chain**. Chips take addresses 1...N. This must succeed
before any addressed command is meaningful.

```
TX   A5 3C | 02 00 | 00 00 | 00 00
RX   A5 3C | 02 00 | nn nn | 00 00        nnnn = chip count
```

### 7.3 Work configuration: 04NN

One payload packet, broadcast immediately before every work load. The controller
always sends the constant `0x00E5`. The field layout is unknown; see section
13.2.

### 7.4 Load work: Y7NN

Broadcast, with the work ID in the tag nibble and the work payload of section 9
as the packet body, CRC included. The expected reply is an all-zero frame of the
same length.

```c
ic_cmd(dev, 0x0400,                   /*addr*/ 0, &(uint16_t){htobe16(0x00E5)}, /*retry*/ 1);
ic_cmd(dev, 0x0700 | (work_id << 12), /*addr*/ 0, work_payload,                 /*retry*/ 5);
```

### 7.5 Poll result: 0800

Broadcast, no payload. An echo of `0800` means the chain has nothing. A reply
command word of `Y8NN` carries one result, decoded in section 10. Poll
repeatedly; there is no other notification path.

### 7.6 Register access: 09NN and 0ANN

```
read
TX   A5 3C | 0A NN | rr rr |                   | 00 00
RX   A5 3C | 1A NN | ss ss | vv vv | ww ww | crc | 00 00     value = vvvv:wwww

write
TX   A5 3C | 09 NN | rr rr | hh hh | ll ll | crc | 00 00
RX   A5 3C | 09 NN | 00 00 | 00 00 | 00 00 |     | 00 00
```

Register numbers and values are transmitted big-endian within their packets. A
read returns the 32-bit value as reply packets 1 and 2, high word first; packet
0 is a status word the controller ignores.

---

## 8 Register map

**Table 8-1. Registers exercised by the controller**

| Reg | Function | Fields | Description | Status |
|---|---|---|---|---|
| 0 | PLL control | [24:16] NF<br>[9:8] OD sel<br>[6:4] NR<br>[7] bypass | Read-modify-write under mask `0xFE00FC8F`. Bit 7 is cleared after loading dividers to engage the new clock. Section 11. | V |
| 3 | Good core count | [7:0] | Read once per chip during bring-up and summed for the chain. A read failure aborts initialization. | V |
| 4 | Chain start | [31:28] see section 13.1<br>[15:0] `0x03F1` | Broadcast once as the final bring-up step; this is what starts the chain hashing. Written, never read. Values observed: `0x800003F1` and `0x700003F1`. Bit 31 is **not** an enable; it is clear on products that hash normally. | V |
| 6 | Sensor mode | bits 3, 2, 0 | Mode 0: set 3 and 2, clear 0. Mode 1: clear 3, set 2, clear 0. Bring-up uses mode 0. | V |
| 7 | Sensor data / period | [31:16] period<br>[11:0] reading | Period written as `ms x 1000000 / 45000`; bring-up passes 25, giving 555. Reading is a raw code converted through section 12. | V |
| `0xFFFF` | Identify | - | A read helper exists in the controller but no product calls it. Response format unknown. | O |

Registers 1, 2, 5 and everything above 7 are untouched by the controller. Their
existence is neither confirmed nor excluded.

---

## 9 Work payload

All three bound parts share the same first 0x50 bytes (the 80-byte
Siacoin-style header) and the same trailer shape: a 64-bit target bound
followed by a 64-bit nonce ceiling. They differ in whether a second header block
sits between them, and in the resulting length.

**Table 9-1. Work payload layouts**

| Offset | ICT580, 96 B | ICC590, 144 B | ICA586, 112 B |
|---|---|---|---|
| `0x00` | 32 B, header block 1 (parent / previous block ID) | same | same |
| `0x20` | 8 B, nonce, starting value | same | same |
| `0x28` | 8 B, timestamp (written as two 32-bit stores) | same | same |
| `0x30` | 32 B, header block 2 (merkle / tree root) | same | same |
| `0x50` | target hi | 48 B, header block 3 | reserved |
| `0x54` | target lo | cont. | cont. |
| `0x58` | nonce ceiling (8 B) | cont. | cont. |
| `0x60` | - | cont. | target hi, then bytes 0x60/0x61 overwritten with 0x13 / 0xDF |
| `0x64` | - | cont. | target lo |
| `0x68` | - | cont. | nonce ceiling (8 B) |
| `0x80` | - | target hi | - |
| `0x84` | - | target lo | - |
| `0x88` | - | nonce ceiling (8 B) | - |

**Header.** Copied verbatim from the block header with no byte swapping. The
chip performs the whole Blake2B compression itself; no midstate is supplied.

**Target.** The top 64 bits of the 256-bit target, as two 32-bit words each
byte-swapped: high word from target bytes [28:32], low word from [24:28]. The
difficulty behind it is `min(device_minimum, work_difficulty)`, so a pool asking
for less work than the device floor gets the device floor.

**Nonce ceiling.** `0x0000FFFFFFFFFFFF` (48 bits) on ICT580 and ICC590;
`0x00003FFFFFFFFFFF` (46 bits) on ICA586.

**Distribution.** Work is broadcast to address 0; every chip receives the same
payload and the same starting nonce. The split across chips and cores is
internal to the chain.

> **[S] The two ICA586 bytes.** On ICA586 the constants `0x13` and `0xDF` are
> stored at payload offsets 0x60 and 0x61 *after* the target words are written
> there, so they overwrite the top two bytes of the target bound. Speculatively
> this pins the top of the comparison window to a fixed value, implying the chip
> compares only a limited range and the upper bits carry something else. No
> evidence either way; it is simply what the code does.

---

## 10 Result report

A reply to `0800` with command word `0xY8NN` carries five payload packets and a
CRC.

**Table 10-1. Result fields**

| Source | Field | Description |
|---|---|---|
| cmd [15:12] | Work ID | The tag of the work item this result belongs to. Results tagged with a retired ID are discarded by the host. |
| cmd [7:0] | Chip address | Which chip produced it. |
| pkt 0-1 | Nonce, low 32 bits | Assembled as `(pkt1 << 16) \| pkt0` and `(pkt3 << 16) \| pkt2`. The 8 bytes written into the header at offset 32 are the low word little-endian followed by the high word little-endian. |
| pkt 2-3 | Nonce, high 32 bits | (as above) |
| pkt 4, low byte | Timestamp index | Which rolled timestamp the result is against. |
| pkt 4, high byte | Core ID | Which core within the chip. |

The host treats two conditions as fatal and re-initializes the chain: ten
consecutive failed polls, and zero results across a whole supervision window.

---

## 11 Clock

Reference is 25 MHz. Setting a frequency is a table search for a post-divider
that puts the feedback divider inside its legal window, then one
read-modify-write of register 0 and one bit clear.

```c
/* identical table in all four back-ends */
static const struct { int div, nr, od; } tbl[] = {
    {48,6,2}, {32,4,2}, {24,6,1}, {16,4,1}, {8,2,1}, {6,6,0}, {2,2,0}, {1,1,0}, {0,0,0}
};

for (i = 0; tbl[i].div; i++) {
    nf = (int)(tbl[i].div * mhz / 25.0);
    if (nf > 0x14 && nf <= 0x77) break;          /* 21 ... 119 */
}
actual_mhz = nf * 25.0 / tbl[i].div;

r0 = read_reg(chip, 0);
r0 = (r0 & 0xFE00FC8F) | (nf << 16) | (tbl[i].od << 8) | (tbl[i].nr << 4);
write_reg(chip, 0, r0);
write_reg(chip, 0, read_reg(chip, 0) & ~0x80);        /* release bypass */
```

Bring-up sets 50 MHz before self-test, then ramps to 500 MHz once the chain has
enumerated. A failure of the ramp is logged but not fatal. Thereafter a thermal
loop walks individual chips down as they approach the product's temperature
target.

---

## 12 Thermal

Per-chip temperature is register 7, low 12 bits, converted through a 156-entry
monotonic lookup table compiled into the controller. Raw `2804` is 125 C and
raw `3420` is -30 C, roughly four counts per degree. The lookup takes the first
entry whose threshold is at or above the raw code; a code past the end of the
table yields -35, the out-of-range sentinel.

This is distinct from the board sensors (one or two TMP112 parts per CPB on
I2C), which provide the intake and exhaust readings and the hard thermal cutoff
(85 C to 90 C depending on product).

---

## 13 Unknowns and speculation

Everything in this section is either unexplained or inferred. None of it is
required to drive the device; the sequence in section 14 works without
understanding any of it.

### 13.1 Register 4, bits [31:28]

**Observation.** Register 4 takes exactly two values across every product
examined: `0x800003F1` and `0x700003F1`. The low 16 bits are identical
everywhere (`0x03F1`, decimal 1009) across chains of 16 to 96 chips and three
chip families. Only the top nibble moves, and it is stable per product across
every firmware version.

**Ruled out.** Not a chip parameter: ICT580 appears with both values, ICC590
appears with both, and ICA586 joins the `7` group. Not CPB count: 3 and 4 CPBs
appear on both sides. Not wifi, which flips between firmware versions of a
single product while the register does not. Not cores per chip (80 and 20 appear
on both sides), and not total cores per chain, which is inverted: 2,880 cores
reads `8` while 1,920 reads `7`.

**Still correlated.** Chips per chain (>=84 vs <=46), series level (>=28 vs <=23),
board rows (>=28 vs <=23) and chips per board row (3 vs 2) all separate the sample
identically and cannot be told apart from these products.

**Table 13-1. Register 4 against topology, all products**

| Product | Chip | Chips/chain | Cores/chip | Series | Board | Register 4 |
|---|---|---|---|---|---|---|
| HS6 | ICC590 | 96 | 20 | 32 | 32 x 3 | `0x700003F1` |
| HS6-SE | ICC590 | 84 | 20 | 32 | 28 x 3 | `0x700003F1` |
| SC6-SE | ICT580 | 84 | 80 | 32 | 28 x 3 | `0x700003F1` |
| SC5 Pro II | ICA586 | 84 | 20 | 28 | 28 x 3 | `0x700003F1` |
| HS3 / HS5 / HS-LITE | ICC590 | 46 | 20 | 23 | 23 x 2 | `0x800003F1` |
| SCBox II | ICT580 | 36 | 80 | 8 | 18 x 2 | `0x800003F1` |
| HS-BOX II | ICC590 | 36 | 20 | 8 | 18 x 2 | `0x800003F1` |
| SCBox / HS-BOX / HS3-SE | ICT580 / ICC590 | 16 | 80 / 20 | 8 | 8 x 2 | `0x800003F1` |

> **[S] Index-budget hypothesis.** The observed values are reproduced exactly, on
> all twelve products, by: *give each chip the largest power-of-two block of
> index slots, capped at 256, such that the whole chain fits in 2^14 slots*,
> equivalently `min(8, 14 - ceil(log2 chips))`. 16, 36 and 46-chip chains take
> 256 slots each; 84 and 96-chip chains cannot fit 256 apiece and drop to 128.
>
> The 14 is not free once the low field is considered. With a step of 1009 a
> 2^24 nonce window holds 16,627 steps, and the largest power of two that fits is
> exactly 2^14: 16,384 slots spanning 16,531,456 nonces, 98.5% of the window. On
> this reading the register holds one arrangement in two fields: 1009 is the
> distance between slots, and the top nibble is how many of the 14 index bits
> each chip keeps once the chain has taken its share. It also motivates 1009
> itself, the largest prime below 1024, which buys the same 14-bit index as a
> stride of 1024 while decorrelating the pattern.
>
> **Against it:** the 2^24 window is inferred and appears nowhere in the
> firmware; a stride of 1024 would give the same 14 bits, so the arithmetic is
> consistent with 1009 rather than evidence for it; the cap at 8 is unexplained;
> and cores per chip never enters, which sits badly with reading the slots as
> core slots: 128 slots is six times what a 20-core part needs. The three rival
> correlations fit the observations equally well.
>
> **Prediction.** 128 chips still reads `7` (128 x 128 = 16,384 exactly); the
> first `6` appears at 129 chips. Nothing in this family passes 96, so any longer
> chain tests it, as would reading register 4 back off live silicon, which no
> firmware here ever does.

### 13.2 The 0x00E5 operand

Command `04NN` is broadcast with the payload `0x00E5` before every work load, on
every product and every chip family. It is a compile-time constant with no
configuration path, so no field structure can be inferred from usage.
**[S]** Speculatively it is a per-work configuration word (a ticket threshold, a
scan-window length, or a mode select) because of where it sits in the sequence.
There is no evidence for any of those readings.

### 13.3 Command 0C00 / 1C00

Defined in all four command tables with a 5-packet request and a 15-packet reply
plus CRC, and issued by nothing. **[S]** The 15-packet reply is three times the
5-packet result frame, so a batched result read is a plausible guess; equally it
could be a production-test or fusing path. No evidence.

### 13.4 Voltage control

Each CPB declares an ISL8118 regulator reached over three GPIOs with a serial
power mode, but the mining controller's voltage setter is a pass-through that
only stores the requested value. **[O]** The rail is therefore programmed by
something else in the system, or is fixed in hardware. Nothing in the mining path
drives it.

### 13.5 Nonce partitioning

Every chip receives an identical broadcast work item with an identical starting
nonce, and results come back tagged with chip and core. How the chain divides the
search space is therefore entirely internal. **[S]** If the 1009 step is a stride
between per-core slots, section 13.1 describes a partition that would work; that
is a hypothesis about the silicon, not an observation of it.

---

## 14 Initialization

The normative bring-up order. Steps 1-5 are board-level; from step 6 everything
is on the bus. Any failure restarts the whole sequence, up to five attempts,
before the chain is declared dead.

1. **Assert reset.** Reset GPIO low, hold 10 ms.
2. **Assert enable low.** Chain enable GPIO low, hold 20 ms.
3. **Set target voltage.** Store the configured rail voltage. Not driven from the mining path, see section 13.4.
4. **Power the board.** Request the power-enable GPIO and drive it high.
5. **Release reset.** Reset low for 50 ms, then high.
6. **Enumerate.** Broadcast `0200` with one zero packet. The reply payload is the chip count.
7. **Configure sensors.** Register 6 to mode 0; register 7 period field to 25 ms (value 555).
8. **Set 50 MHz.** PLL sequence of section 11, broadcast.
9. **Assert enable high.** Enable GPIO high, hold 20 ms.
10. **Self-test.** Broadcast `0100`, then `0B00`, then `0300`.
11. **Read good cores.** Register 3 from each chip 1...N; sum for the chain.
12. **Ramp to 500 MHz.** Adaptive clock step. Failure here is logged, not fatal.
13. **Start hashing.** Broadcast write of register 4 (section 8), then wait 5 s before the first work load.

---

## 15 Board signals

**Table 15-1. Per-CPB host signals, from the controller's configuration file**

| Signal | Type | Function |
|---|---|---|
| `reset` | GPIO out | Chain reset, active low. Pulsed at bring-up. |
| `enable` | GPIO out | Chain enable. Held low across PLL setup, raised before self-test. |
| `plugin` | GPIO out | Board power enable. Driven high once at bring-up. |
| `power.gpio0/1/2` | GPIO out | ISL8118 rail select. Declared but not driven by the mining path. |
| `tsensor` | I2C | One or two TMP112 per CPB, addresses 0x48 / 0x4A. |
| `spipath` | SPI | One spidev node per CPB. |
| `fans` | PWM + tach | Two to four fans per miner, shared across CPBs. |

---

## 16 Provenance

The interface described here was established by static analysis of the `gsb3`
back-end of Goldshell's `intminer`, an ARM Thumb-2 program, across 63 firmware
images: 46 `.cpb` update packages covering 11 products, plus one 256 MB factory
burn image. No silicon documentation was consulted and no bus capture was taken,
so **nothing here has been confirmed against hardware.**

The code listings in sections 5, 7 and 11 were written for this document to
express the algorithms in a readable form. They are illustrations, not
disassembly output.

Everything here describes what one controller implementation does, which is a
lower bound on what the silicon can do. Where the firmware and the device
disagree, the device is right. Two areas are most likely to hide a discrepancy:
the meaning of fields the controller writes blindly and never reads back
(register 4, the `0x00E5` operand), and any device behavior the controller
never exercises: registers 1, 2 and 5, command `0C00`, and whatever the chain
does when a command arrives out of the expected order.

---

## Appendix A: firmware containers

How to get from a distributed firmware file to the controller binary. Three
layers, none encrypted; the signature files that accompany the payloads are not
checked anywhere in the extraction path.

### A.1 .cpb update package

A concatenation of sections, each with a 512-byte header: a NUL-padded name in
bytes 0-127 and the payload length as a little-endian `uint32` at offset `0x80`.
The payload, a gzipped tar, begins immediately after the header, and the next
header is 512-byte aligned. Headers after the first are written into a dirty
buffer, so only the name field is reliable; scanning for gzip magic at 512-byte
boundaries and inflating is more robust than trusting the declared length.

### A.2 .ius firmware image

Each tar holds `firmware.ius` plus a detached signature. The image is
Ingenic-style: magic `+ius`, version string at `0x10`, section count at `0x1c`,
then 16-byte descriptors from `0x2c` of the form
`{u32 size, u32 dest_lba, u32 index, u32 erase_block}`. Payloads start at
`0x200`, each padded to a 1 KiB boundary. Sections are u-boot, an environment
blob, a ramdisk uImage, one or two kernel uImages, and the root filesystem.

### A.3 Root filesystem

Squashfs on the small images, a 32 MiB ext4 volume on the large ones;
`unsquashfs` and `debugfs -R rdump` respectively. The controller is
`/usr/bin/intminer` and the board description is `/root/product.json`. Factory
burn images are a different shape: a 256 MB eMMC image with an MBR, one empty
FAT configuration partition, and the boot chain written raw outside it at fixed
sector offsets rather than at the `dest_lba` values the `.ius` descriptors carry.

---

*Intchains ASIC host interface, rev 0.2, established by static analysis of
Goldshell intminer 2.2.x. Status of every claim is marked inline: V verified,
O observed, S speculative.*
