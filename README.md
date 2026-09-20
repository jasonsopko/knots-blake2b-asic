# knots-blake2b-asic

Mining hardware for the Bitcoin Knots BLAKE2b chain. Three things that turned
out to be one thing:

- **A datasheet and a driver** for the Intchains Blake2B ASICs in Goldshell
  miners, so the parts already in the field can be driven by software that is
  not the vendor's.
- **The Knots work layer**, which is what makes those parts mine this chain.
- **A specification and a reference engine for proof-of-work profile 1**, the
  one profile no shipping part can hash.

Nothing here has touched silicon. Everything is checked against the Bitcoin
Knots test data and against the chain itself, in software, and the places where
that is not enough are written down.

## Credit, before anything else

The hardest part of this was already done by somebody else. The Intchains
interface was reverse engineered and specified by Luke Dashjr, who published it
at:

<https://luke.dashjr.org/tmp/code/intchains-asic-datasheet.html>

and posted that link in the #mining channel of the Bitcoin Knots Discord on
2026-09-07, which is where I found it.

`docs/datasheet.md` is a transcription of that document and
`docs/datasheet-rev0.2.source.html` is the original as it was served. Without
it none of the rest of this repository could exist, and nothing in it is a
substitute for reading the original.

The proof of work itself, and its four profiles, are Luke's design as well.
`src/primitives/block.cpp` and `src/test/data/block_header_v2.json` in the
Bitcoin Knots tree are the source of truth this repository is checked against.

What is mine is the C, the RTL, the tests and `docs/profile-1.md`.

The name says Knots because that is what drove the work, but only the middle
piece is chain-specific. `docs/datasheet.md` and the `intchains.h` half of the library describe and
drive the Intchains parts themselves, and are as useful to someone pointing a
Goldshell at Siacoin or Handshake as to someone pointing it here. If that is
you, `intchains_knots.h` and the three files behind it (`ic_knots.c`,
`ic_blake2b.c`, `ic_sha256.c`) are the only part you can ignore.

## The short version

Knots v2 headers are 164 bytes, and the proof of work is not taken over the
header. It runs over a short buffer the node folds the header down to, and the low two bits of
`m_flags` pick which buffer it builds.

| Profile | Buffer | The 16 varied bytes sit at | Hashed by |
|---|---|---|---|
| 0 | 80 B | `0x20..0x2F` | ICT580, ICA586, unchanged |
| 1 | 80 B | `0x00..0x0F` | nothing that ships |
| 2 | 128 B | `0x50..0x5F` | ICC590's length, wrong offset |
| 3 | 160 B | `0x70..0x7F` | nothing that ships |

Profile 0 is the Siacoin layout byte for byte, including where the chip rolls,
so a Goldshell SC-series miner can mine this chain with no change to the
silicon. `nNonce` and `m_nonce2` land under the chip's nonce, `m_time_offset`
and `m_nonce3` under its timestamp, and when the `UseTimeOffset` flag is clear
the timestamp roller is free nonce space.

Profile 1 is the same length with the varied bytes moved to the front and the
tail reordered, and nothing has ever mined it. Luke Dashjr has said it would be
nice if someone started making ASICs for it. `docs/profile-1.md` is what such a
chip has to compute, and `rtl/` is a reference that does it.

A regtest chain has accepted blocks built to all four layouts, profile 1
included, with the proof of work computed by this repository's own library.
As far as I can tell those are the first profile 1 blocks that have existed.
`tools/mine-regtest.py` reproduces it. That does not say a profile 1 chip would
work; it says the specification is complete enough that a chip built to it
would have its work accepted.

## Layout

```
docs/datasheet.md        the Intchains host interface, rev 0.2, transcribed
docs/profile-1.md        what a profile 1 chip has to compute
docs/open-questions.md   everything unresolved, ours and the datasheet's
include/, src/           the host library: SPI, framing, registers, work, Knots
tools/ic-probe.c         read-only first contact with a chain
tools/ic-verify-block.c  rebuild a real block's PoW and check it
tools/chain-headers.sh   feed every block since the fork to the above
tools/ic-grind.c         find a nonce for a v2 header, any profile
tools/mine-regtest.py    mine a block of a chosen profile on regtest
tools/gen-vectors.c      golden vectors for the RTL, from the same library
tests/                   C tests, run against a chain simulator
rtl/, tb/                the profile 1 reference engine and its testbenches
```

## Build

```
make            library, tools and tests
make check      the C tests, no hardware needed
make vectors    regenerate the golden vectors for the RTL
make sim        run the Verilog testbenches (wants iverilog)
make cross      build for a miner's ARM SoC (wants gcc-arm-linux-gnueabihf)
make synth      ECP5 area figures for both engines (wants yosys)
make pnr        place and route (wants nextpnr-ecp5; see the caveat below)
```

Only `tools/chain-headers.sh` needs a running node. Everything else, tests and
simulation included, builds and runs on its own.

## Parts

| Part | Cores/chip | Work payload | Products |
|---|---|---|---|
| ICT580 | 80 | 96 B | SCBox, SCBox II, SC6-SE |
| ICC590 | 20 | 144 B | HS3, HS3-SE, HS5, HS6, HS6-SE, HS-BOX, HS-BOX II, HS-LITE |
| ICA586 | 20 | 112 B | SC5 Pro II |
| ICC551 | unknown | 96 B | none, compiled in but never bound |

Resolve the part by part number. The `chips.model` string in a miner's
configuration is never compared against anything in the vendor's controller,
and at least one product declares a name with no back-end behind it.

## First contact with a chain

`build/ic-probe` is read-only: it enumerates the chain and reads the good core
count and temperature off every chip. It does not start anything hashing.

```
ic-probe -d /dev/spidevN.M -p ICA586
ic-probe -d /dev/spidevN.M -p ICC590 -v --selftest
```

`-v` hexdumps both directions of every transfer, which is where to start when
the chain reads as dead.

Nothing has to be flashed to run it. If you can get a shell on the miner, the
whole procedure is: stop the vendor's controller, copy this to `/tmp`, run it,
and power cycle to put everything back. Whether you can get that shell is not
something this repository knows; I have never had one of these open. Build it
`-static` so an old or non-glibc userland does not matter, and take the spidev
path from the miner's own `/root/product.json`. The `spidevN.M` above is a
placeholder, not a known-good path.

## Checked against the chain, not only against test data

`tools/ic-verify-block` takes a serialized v2 header and the hash the chain
gives it, rebuilds the buffer a mining chip would have hashed, and compares.

```
tools/chain-headers.sh | build/ic-verify-block -
```

Every block since the fork at 961640 reproduces: 11,369 of them as of height
973008, none mismatched, including the ones that set an XOR key and the ones
that use a time offset.

That run also answers the question this repository exists for. Every one of
those blocks is profile 0, the Siacoin layout that ICT580 and ICA586 hash
unchanged. Not one has been mined with profile 1, 2 or 3. The whole network is
one part family, and the profile left for new entrants has never been used.

## How the Knots side is checked

`tests/test_knots.c` runs all five header vectors from
`src/test/data/block_header_v2.json` in the Knots tree through every stage of
`CBlockHeader::GetHash()` and compares h1, h2, both BLAKE2b stages, the ASIC
input buffer, the XOR mask and the block hash. All four profiles agree on all
of them.

The chip compares only the top 64 bits of its own digest, so the host has to
hash every result properly before it is worth submitting. `ic_knots_pow_hash`
is that check, and `ic_knots_compare_value` is what the chip itself sees.

When a pool sets an XOR key whose mask has any of its top 64 bits set, the
chip's comparison and the node's are about different numbers. That is
deliberate: it is how a pool stops a miner recognizing its own block.
`ic_knots_asic_target_exact` says which case you are in.

## What is not here

No pool or stratum client, no work generation, no thermal control loop, no fan
control, no TMP112 driver, no GPIO backend, and no multi-board supervision.

The thermal conversion is the weakest part of the driver. The vendor's
controller carries a 156-entry lookup table that is not in the datasheet, so
`ic_temp_from_raw` interpolates the two published anchors and will be a degree
or two off. `ic_temp_set_table` takes the real table once somebody lifts it out
of a firmware image.

The RTL has been simulated, synthesized and placed. It has never been on a
board.

There are two engines. `profile1_miner` runs one hash at a time in sixteen
cycles. `profile1_miner_pipe` unrolls the twelve rounds and retires one hash
per clock, for four times the area and sixteen times the throughput, which is
four times the work per slice. The reason is not the pipelining. With the round
index in a register every message selection is a sixteen-way multiplexer over a
64-bit word, and that is most of the iterative core; unrolled, the schedule is
fixed at elaboration and a selection is a wire. `docs/profile-1.md` has the
cell counts, and `make synth` reproduces them.

The iterative engine packs into 19,241 LUTs and places between 9.5 and 11.6 MHz
on an ECP5, which at sixteen cycles a hash is about 0.6 MH/s. Those are
placement figures: four routing attempts across three parts never finished, and
`docs/profile-1.md` says why and what it cost. Treat the clock as an order of
magnitude, not a specification.

It is the expected order of magnitude, and it comes from depth rather than
area: a round is roughly twelve dependent 64-bit additions and one round
between registers is deep. The architectural conclusion is to pipeline inside
the round, not at round boundaries. Neither engine here does that.

It also puts the FPGA in perspective. Even the unrolled engine at one hash per
clock would be around 10 MH/s, against 11 TH/s for a shipping part. An FPGA
here is a correctness proof and something to point at, never a miner.

There is no timing closure, no area or power target and no floorplan.

One thing not worth doing: chasing precomputation. The varied words land in a
single G function of round 0, so three of the ninety-six G functions can be
hoisted out. That is three percent. There is no midstate here the way there is
in Bitcoin's double SHA-256, on any of the four profiles.

## License and credit

MIT, in `LICENSE`. That covers the code, the tests, the RTL and the documents I
wrote.

It does not cover `docs/datasheet.md` or `docs/datasheet-rev0.2.source.html`.
Those are Luke Dashjr's work, reproduced here with attribution and a link to
the original at <https://luke.dashjr.org/tmp/code/intchains-asic-datasheet.html>,
and the terms on that text are his to set, not mine. If you want to reuse the
datasheet rather than the code, ask him.
