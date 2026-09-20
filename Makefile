# Blake2B mining ASICs: the Intchains host interface, Bitcoin Knots work, and
# a reference engine for proof-of-work profile 1.
#
#   make            library, tools and tests
#   make check      run the C tests (no hardware needed)
#   make vectors    regenerate the golden vectors for the RTL
#   make sim        run the Verilog testbenches against those vectors
#   make wave       the same run, with waveforms in gtkwave
#   make synth      yosys area figures for both engines (wants yosys)
#   make pnr        place and route for a clock figure (wants nextpnr-ecp5)
#   make cross      build for a miner's ARM SoC
#   make clean

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c99 -Wall -Wextra -Wshadow -Wstrict-prototypes -Iinclude
BUILD   ?= build

LIB_SRC := $(wildcard src/*.c)
LIB_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(LIB_SRC))
LIB     := $(BUILD)/libintchains.a

TOOL_SRC := $(wildcard tools/*.c)
TOOLS    := $(patsubst tools/%.c,$(BUILD)/%,$(TOOL_SRC))

TEST_SRC := $(filter-out tests/fake_chain.c,$(wildcard tests/test_*.c))
TESTS    := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRC))

# The miner runs an ARM Thumb-2 userland on Linux 3.10.
CROSS_CC ?= arm-linux-gnueabihf-gcc

IVERILOG ?= iverilog
VVP      ?= vvp

.PHONY: all check vectors sim wave synth pnr cross clean

all: $(LIB) $(TOOLS) $(TESTS)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/%: tools/%.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Itools $< $(LIB) -o $@

$(BUILD)/%: tests/%.c tests/fake_chain.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Itests $< tests/fake_chain.c $(LIB) -o $@

check: $(TESTS)
	@fail=0; for t in $(TESTS); do ./$$t || fail=1; done; \
	 if [ $$fail -eq 0 ]; then echo "all tests passed"; else echo "TESTS FAILED"; fi; \
	 exit $$fail

# ------------------------------------------------------------ profile 1 RTL

vectors: $(BUILD)/gen-vectors
	./$(BUILD)/gen-vectors

sim: vectors $(BUILD)/tb_core $(BUILD)/tb_miner $(BUILD)/tb_pipe
	@$(VVP) $(BUILD)/tb_core
	@$(VVP) $(BUILD)/tb_miner
	@$(VVP) $(BUILD)/tb_pipe

# Same run with waveforms, then open them.
wave: vectors $(BUILD)/tb_miner
	@$(VVP) $(BUILD)/tb_miner +vcd
	@gtkwave $(BUILD)/tb_miner.vcd >/dev/null 2>&1 &

$(BUILD)/tb_core: tb/tb_blake2b_core.v rtl/blake2b_core.v | $(BUILD)
	@command -v $(IVERILOG) >/dev/null || \
	    { echo "$(IVERILOG) not found: apt install iverilog"; exit 1; }
	$(IVERILOG) -g2005 -I rtl -o $@ $^

$(BUILD)/tb_miner: tb/tb_profile1_miner.v rtl/profile1_miner.v rtl/blake2b_core.v | $(BUILD)
	@command -v $(IVERILOG) >/dev/null || \
	    { echo "$(IVERILOG) not found: apt install iverilog"; exit 1; }
	$(IVERILOG) -g2005 -I rtl -o $@ $^

$(BUILD)/tb_pipe: tb/tb_profile1_pipe.v rtl/profile1_miner_pipe.v | $(BUILD)
	@command -v $(IVERILOG) >/dev/null || \
	    { echo "$(IVERILOG) not found: apt install iverilog"; exit 1; }
	$(IVERILOG) -g2005 -I rtl -o $@ $^

# ---------------------------------------------------- area and clock figures
#
# The target device is a Lattice ECP5 85K, which is the smallest part the
# unrolled engine fits in. Nothing here has been on a board; these are what the
# open toolchain reports, not measurements.

YOSYS   ?= yosys
NEXTPNR ?= nextpnr-ecp5
# The iterative engine packs into 19,241 LUTs: 79 percent of an LFE5U-25F, 43
# percent of a 45F, 23 percent of an 85F. The unrolled engine is four times the
# size and wants an 85F. Override DEVICE for whatever board you have; be aware
# the 25F fit is congested and routing suffers for it.
DEVICE  ?= --25k
PACKAGE ?= CABGA381
# Ask for something reachable. One BLAKE2b round between registers is about
# twelve dependent 64-bit additions, which places at around 10 MHz here, and a
# target the router cannot meet makes it thrash instead of converging.
FREQ    ?= 10
# Place and route logs live outside $(BUILD) on purpose: a run takes a long
# time, and "make clean" during one would delete the log out from under it.
PNRLOG  ?= /tmp
RTL     := rtl/fpga_top.v rtl/profile1_miner.v rtl/profile1_miner_pipe.v rtl/blake2b_core.v

synth: $(BUILD)/iter.json $(BUILD)/pipe.json
	@for n in iter pipe; do \
	    echo "== $$n =="; \
	    sed -n '/Printing statistics/,/^$$/p' $(BUILD)/$$n.synth.log | \
	        grep -E "CCU2C|LUT4|TRELLIS_FF|L6MUX21|PFUMX" | tail -5; \
	done

$(BUILD)/iter.json: $(RTL) rtl/blake2b_defs.vh | $(BUILD)
	@command -v $(YOSYS) >/dev/null || { echo "$(YOSYS) not found"; exit 1; }
	$(YOSYS) -p "read_verilog -I rtl $(RTL); chparam -set PIPELINED 0 fpga_top; \
	    synth_ecp5 -top fpga_top -json $@" > $(BUILD)/iter.synth.log 2>&1

$(BUILD)/pipe.json: $(RTL) rtl/blake2b_defs.vh | $(BUILD)
	@command -v $(YOSYS) >/dev/null || { echo "$(YOSYS) not found"; exit 1; }
	$(YOSYS) -p "read_verilog -I rtl $(RTL); chparam -set PIPELINED 1 fpga_top; \
	    synth_ecp5 -top fpga_top -json $@" > $(BUILD)/pipe.synth.log 2>&1

# Warning: routing has never completed here. Four attempts across three ECP5
# parts stalled partway, so what you get back is a placement figure. See the
# routing section of docs/profile-1.md.
pnr: $(BUILD)/iter.json $(BUILD)/pipe.json
	@command -v $(NEXTPNR) >/dev/null || \
	    { echo "$(NEXTPNR) not found: apt install nextpnr-ecp5"; exit 1; }
	@for n in iter pipe; do \
	    $(NEXTPNR) $(DEVICE) --package $(PACKAGE) --json $(BUILD)/$$n.json \
	        --textcfg $(BUILD)/$$n.config --freq $(FREQ) > $(PNRLOG)/$$n.pnr.log 2>&1 || true; \
	    echo "== $$n =="; \
	    grep -iE "Max frequency for clock" $(PNRLOG)/$$n.pnr.log | tail -1; \
	done

cross:
	@command -v $(CROSS_CC) >/dev/null || \
	    { echo "$(CROSS_CC) not found: install gcc-arm-linux-gnueabihf"; exit 1; }
	$(MAKE) CC=$(CROSS_CC) BUILD=build-arm all

clean:
	rm -rf $(BUILD) build-arm tb/*.mem
