// blake2b_core.v - one BLAKE2b compression, one round per clock.
//
// Twelve rounds, twelve cycles, plus a cycle to load. Nothing is pipelined:
// this is a reference you can read against RFC 7693, not a throughput design.
// See rtl/profile1_miner_pipe.v for the unrolled form, which is four times the
// throughput per slice because the message schedule stops being a multiplexer.
//
// Contract: hold m, h_in, t and final_block stable from the cycle start is
// asserted until done. Nothing is latched.

`default_nettype none

module blake2b_core (
    input  wire         clk,
    input  wire         rst_n,

    input  wire         start,
    input  wire [1023:0] m,          // m[i] is bits [i*64 +: 64]
    input  wire [63:0]  t,           // byte counter, low word
    input  wire         final_block,
    input  wire [511:0] h_in,        // h[i] is bits [i*64 +: 64]

    output reg  [511:0] h_out,
    output reg          done,
    output reg          busy
);

`include "blake2b_defs.vh"

    reg [1023:0] v;
    reg [3:0]    round;
    integer      i;
    genvar       gi;

    // The message words this round wants. round is a register here, so each
    // selection is a sixteen way multiplexer. That is most of the area.
    wire [1023:0] ms;
    generate
        for (gi = 0; gi < 16; gi = gi + 1) begin : sel
            assign ms[gi*64 +: 64] = m[SIGMA[(round*16+gi)*4 +: 4]*64 +: 64];
        end
    endgenerate

    wire [1023:0] nv = round_fn(v, ms);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            busy  <= 1'b0;
            done  <= 1'b0;
            round <= 4'd0;
            h_out <= 512'd0;
            v     <= 1024'd0;
        end else begin
            done <= 1'b0;

            if (!busy) begin
                if (start) begin
                    v <= { IV7,
                           final_block ? ~IV6 : IV6,
                           IV5,
                           IV4 ^ t,
                           IV3, IV2, IV1, IV0,        // v[11:8]
                           h_in };                    // v[7:0]
                    round <= 4'd0;
                    busy  <= 1'b1;
                end
            end else begin
                v <= nv;

                if (round == 4'd11) begin
                    for (i = 0; i < 8; i = i + 1)
                        h_out[i*64 +: 64] <= h_in[i*64 +: 64]
                                           ^ nv[i*64 +: 64]
                                           ^ nv[(i+8)*64 +: 64];
                    busy <= 1'b0;
                    done <= 1'b1;
                end else begin
                    round <= round + 4'd1;
                end
            end
        end
    end

endmodule

`default_nettype wire
