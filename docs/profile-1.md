# Bitcoin Knots proof-of-work profile 1

What a mining chip for profile 1 has to compute. Written as an implementation
target: everything that goes into the hash, in order, and the comparison that
decides whether a result is worth reporting.

Source of truth is `CBlockHeader::GetHash()` in `src/primitives/block.cpp` of
Bitcoin Knots. Every claim here is checked against
`src/test/data/block_header_v2.json` in the same tree.

The proof of work this describes and its four profiles are Luke Dashjr's
design. Why there are four, and why one of them matches no existing part, is
his to explain and not something this document guesses at. All it does is write
down what a chip would have to compute to hash profile 1. Where it and the
Knots source disagree, the source is right.

## 1 Why profile 1 exists

Knots v2 block headers are 164 bytes. Nothing hashes a 164-byte header. The
proof-of-work runs over a short buffer that the node folds the header down to,
and the low two bits of `m_flags` pick which buffer it builds. There are four:

| Profile | Buffer | The 16 varied bytes sit at | Hashed by |
|---|---|---|---|
| 0 | 80 B | `0x20..0x2F` | Siacoin parts, unchanged |
| 1 | 80 B | `0x00..0x0F` | nothing that ships |
| 2 | 128 B | `0x50..0x5F` | nothing that ships |
| 3 | 160 B | `0x70..0x7F` | nothing that ships |

Profile 0 is the Siacoin header layout byte for byte, including where the
varied fields sit, so the Blake2B parts already in Goldshell SC-series miners
hash it without a change. Profile 1 is the same length with the varied fields
moved to the front of the buffer and the tail reordered. No shipping part does
that, which is the point of this document.

## 2 The buffer

80 bytes, in this order:

| Offset | Size | Field | Varies |
|---|---|---|---|
| `0x00` | 4 | `nNonce` | yes |
| `0x04` | 4 | `m_nonce2` | yes |
| `0x08` | 4 | `m_nonce3` | yes |
| `0x0C` | 4 | `m_time_offset` | yes |
| `0x10` | 32 | `hash1`, the Sv1 fields folded to one value | no |
| `0x30` | 32 | `h2`, the merge-mining hook | no |

All four 32-bit fields are little-endian. The two 32-byte values are opaque to
the chip: it copies them in and never interprets them.

Note the field order. Profile 0 puts `m_time_offset` third and `m_nonce3`
fourth; profile 1 swaps them. A controller that reuses a profile 0 result
decoder on profile 1 work reports the wrong header.

The chip never sees the block version, the time, the difficulty or the merkle
root. That is deliberate: it means a chip cannot brick itself at some future
block version, time or difficulty, because it has no opinion about any of them.

## 3 The hash

One BLAKE2b compression. Not two, and no midstate.

- Unkeyed BLAKE2b, 32-byte digest.
- The 80-byte buffer is zero-padded to one 128-byte block.
- Parameter block: `h[0] ^= 0x01010020`, which is fanout 1, depth 1, no key,
  digest length 32. The other seven chaining words are the IV unchanged.
- Byte counter `t = 80`. Final-block flag set, so `v[14]` is inverted.
- Twelve rounds, standard BLAKE2b message schedule.

The digest is `h[0..3]`, each stored little-endian, which gives 32 bytes.

### 3.1 There is nothing to precompute

Round 0 uses the identity message schedule, so the varied words `m[0]` and
`m[1]` both enter the first column G function. The other three column G
functions do not depend on them and can be computed once per work item. Every
diagonal G in round 0 consumes a word the first column touched, so from there
on everything moves.

That is 3 of the 96 G functions in a full compression, near enough 3 percent.
Profiles 2 and 3 save 4 rather than 3. Whichever profile you build, there is no
useful midstate, unlike Bitcoin's double SHA-256 where the first block is
precomputed and only the second is ground.

Build for a full compression per nonce. Anyone quoting a design that skips most
of the work has misread the layout.

## 4 The comparison

The proof-of-work value is the digest XORed with a mask the pool controls, most
significant byte first. Byte 0 of the digest is the most significant byte of
the value.

A chip is not expected to do any of that. It compares the leading 64 bits of
its own digest against a 64-bit bound the host supplies:

```
    report the nonce when  bswap64(h[0])  <=  bound
```

`h[0]` is the first chaining word out of the compression. Byte-swapping it
gives the digest's first eight bytes read as a big-endian number, which is the
top 64 bits of the proof-of-work value.

Two things follow, and both belong in the host software rather than the chip.

The chip's comparison is on the unmasked digest. When the pool sets an XOR key
whose mask has any of its top 64 bits set, the chip's answer and the node's
answer are about different numbers. That is intentional, it is how a pool stops
a miner recognizing its own block, and it means the host has to hash every
reported result properly before deciding anything.

Sixty-four bits is a bound, not a target. The host runs the full hash on every
result. A chip that reports too much is wasteful; a chip that reports too
little loses blocks.

## 5 What the host has to be able to do

Minimum for a usable part, stated as obligations rather than as a bus design:

- Load an 80-byte work buffer.
- Set the 64-bit bound.
- Set the starting value of the varied bytes, and a ceiling if the counter is
  narrower than 64 bits.
- Report, for every hit: the bytes that produced it, and enough identity to
  tell which work item and which core it came from.

The last one is worth spelling out. If a chip reports an index into some
internal sequence rather than the bytes themselves, the host cannot rebuild the
header without knowing the sequence. Report the bytes.

## 6 Test vectors

`tb/core_vectors.mem` and `tb/miner_vector.mem` are generated by
`tools/gen-vectors`, which refuses to write anything until it reproduces
`profile_1_time_offset_nonzero_key` from that file. Vector 0 is that vector, so
this document is tied to the node's own test data rather than to a buffer
someone typed in. The generator uses the same library the host controller does,
so a simulation and a controller cannot drift apart.

Run `make vectors` to regenerate them and `make sim` to run the testbenches.

## 7 What it costs, measured

Two engines are in `rtl/`. `profile1_miner` runs one hash at a time through a
single round-per-clock core. `profile1_miner_pipe` unrolls the twelve rounds
into twelve stages and retires one hash per clock.

Yosys for a Lattice ECP5, cell counts before packing:

| | LUT4 | CCU2C | FF | L6MUX21 | PFUMX | hash/clock |
|---|---|---|---|---|---|---|
| iterative | 15,925 | 1,120 | 2,060 | 826 | 2,868 | 1/16 |
| unrolled | 49,261 | 12,095 | 13,136 | 12 | 31 | 1 |

Four times the area for sixteen times the throughput, so about four times the
work per slice. Look at the two multiplexer columns for the reason. When the
round index is a register, every one of the sixteen message selections is a
sixteen-way multiplexer over a 64-bit word, and that is most of the iterative
core. Unrolled, the message schedule is fixed at elaboration and a selection
becomes a wire: 826 L6MUX21 falls to 12.

That is the one design decision worth taking from this. It applies to silicon
as much as to an FPGA, and it is the opposite of the usual reason for
unrolling. The throughput is a bonus; the multiplexer is the cost.

Taking an ECP5 slice as two LUT4 or one carry cell, the iterative engine works
out at about 9,100 slices and the unrolled one at about 36,700. An LFE5U-85F
has 41,820, so it holds one unrolled engine or four iterative cores.

Those are yosys figures before packing, but the estimate has been checked once:
nextpnr packs the iterative engine into 19,241 LUTs, which is 9,620 slices
against the 9,100 predicted, 5 percent low. The unrolled figure has not been
checked the same way, so treat it as an estimate with roughly that confidence
rather than a measurement.

### The clock, and what it implies

The iterative engine places between 9.5 and 11.6 MHz on an ECP5, depending on
the part and on what you asked nextpnr for. At sixteen cycles a hash that is
roughly 0.6 MH/s, and the unrolled engine at one hash per clock would be around
10 MH/s on the same silicon.

Read those figures with two caveats. nextpnr stops optimizing placement once it
meets the target frequency it was given, so a run that passes reports a lower
bound rather than a maximum. And no run here was routed, only placed, so the
real numbers are somewhat lower than these.

| Part | Occupancy | Target | Placed | Result |
|---|---|---|---|---|
| LFE5U-25F | 79% | 10 MHz | 11.56 MHz | pass, so a lower bound |
| LFE5U-45F | 43% | 12 MHz | 9.55 MHz | fail, pushed and fell short |
| LFE5U-85F | 23% | 100 MHz | 10.30 MHz | fail, pushed and fell short |

What the spread does not support is a conclusion about which part is best. The
targets differ, so the numbers are not comparable with each other. What they do
agree on is the order of magnitude, and that agreement across three parts is
the useful result: this design runs at about 10 MHz on an ECP5 and the part
does not change that much.

That is a depth result, not an area one. The G function is six dependent 64-bit
additions and a round is two layers of G, so roughly twelve additions sit
between registers in both engines. One round per clock is too deep for a useful
clock. A design that wants one pipelines inside the round, cutting between the G
layers or finer, trading flip-flops for depth. Neither engine here does that.

It also sizes the gap honestly. Ten megahashes against eleven terahashes for a
shipping part is six orders of magnitude, which is the usual distance between an
FPGA and a hashing ASIC. Build the FPGA to prove the design is right in real
logic, not to mine with it.

### Routing does not finish, and you should know that before you start

Four attempts across three parts and about eighty minutes of router time, and
not one completed. The best got 45 percent of the 65,657 arcs placed before the
rate decayed far enough to be worth abandoning. So the figures above are all
post-placement.

The likely reason is in the design rather than the tool. The engine is mostly
wide adders: 1,120 carry cells forming 64-bit chains that have to sit in
contiguous columns. That constrains placement hard and leaves the router
little room to move, and it gets worse as occupancy rises, which is why the
79 percent fit on the 25F was no faster than the 23 percent fit on the 85F.

If you need a routed number, expect to leave it running a long time, and try
pipelining inside the round first: shorter combinational runs between registers
should relieve both the timing and the congestion.

## 8 A block was mined to this document

The layouts above are not only checked against header vectors. A regtest chain
with the fork scheduled has accepted blocks built to every one of them:

| Height | Profile | Buffer | Result |
|---|---|---|---|
| 9 | 1 | 80 B | accepted |
| 10 | 2 | 128 B | accepted |
| 11 | 3 | 160 B | accepted |
| 12 | 1 | 80 B | accepted |
| 13 | 0 | 80 B | accepted |

The proof of work for those blocks came from `build/ic-grind`, which uses this
repository's library, so what the node validated is what this document
describes rather than a second implementation of it. `tools/mine-regtest.py`
reproduces the whole thing, and `build/ic-verify-block` reads the results back
and agrees.

As far as I can tell these are the first profile 1 blocks that have existed.
Every block on mainnet since the fork is profile 0.

Two things this settles. The document is complete enough to build from: a chip
that produced these buffers would have its work accepted. And consensus does
not constrain the choice, because `CheckBlockHeader` reserves only the top two
bits of `m_flags` for future hardforks and leaves the profile selector alone.

Two things it does not settle. The blocks were mined by a CPU, so nothing here
says a chip built to this document would work, only that its output would be
accepted. And regtest difficulty is trivial, so the search says nothing about
performance.

## 9 Status

Everything above is verified in software against the Knots tree, and the
layouts are verified against a node that accepted blocks built to them.
Nothing has been verified against silicon, because there is no silicon.

The RTL in `rtl/` simulates clean against these vectors under Icarus Verilog:
the compression core reproduces all eight, and the search engine finds the same
nonce the model found and reports nothing above the bound. It runs one hash at
a time in sixteen cycles, which is twelve rounds plus the load, the start, the
finish and the compare. It has been synthesized and placed but never routed to
a board, and it is written to be read against RFC 7693 rather than to be
efficient.
