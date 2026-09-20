// profile1_miner_pipe.v - the same search, unrolled.
//
// Twelve round stages, one hash finishing every clock. The point of unrolling
// is not only throughput: with one round per stage the message schedule is
// fixed at elaboration, so selecting a message word costs wiring instead of a
// sixteen way multiplexer. In the iterative core that multiplexer is most of
// the area.
//
// Only the nonce rides down the pipeline. Everything else in the work buffer
// is the same for every hash in flight, so it stays in one register.

`default_nettype none

module profile1_miner_pipe (
    input  wire        clk,
    input  wire        rst_n,

    input  wire        load,
    input  wire [575:0] work_fixed,     // m[1..9]
    input  wire [63:0] bound,
    input  wire [63:0] nonce_start,
    input  wire [63:0] nonce_ceiling,

    output reg         hit,
    output reg  [63:0] hit_nonce,
    output reg  [63:0] hit_value,
    output reg         exhausted,
    output reg         busy,
    output reg  [63:0] hashes
);

`include "blake2b_defs.vh"

    localparam [63:0] H0 = IV0 ^ 64'h0000000001010020;
    localparam [63:0] MSG_BYTES = 64'd80;

    // v after initialization, before round 0. None of it depends on the
    // message, so it is a constant.
    localparam [1023:0] V_INIT = {
        IV7, ~IV6, IV5, IV4 ^ MSG_BYTES,
        IV3, IV2, IV1, IV0,
        IV7, IV6, IV5, IV4, IV3, IV2, IV1, H0
    };
    localparam [511:0] H_IN = {
        IV7, IV6, IV5, IV4, IV3, IV2, IV1, H0
    };

    reg [575:0] fixed;
    reg [63:0]  bound_r, ceiling_r, nonce;
    reg         feeding;

    // Pipeline: stage s holds the state after round s-1, with the nonce that
    // produced it and a valid bit.
    reg [1023:0] v_s   [1:12];
    reg [63:0]   n_s   [1:12];
    reg          val_s [1:12];

    integer i;
    genvar gs, gw;

    // The message a given nonce produces. m[0] is the nonce, m[1..9] the work,
    // m[10..15] the zero padding. Only m[0] differs between hashes in flight.
    wire [1023:0] msg0 = {384'd0, fixed, nonce};

    // Stage 0 applies round 0 to the constant initial state. The sigma index
    // is fixed at elaboration, so each selection is wiring, not a multiplexer.
    wire [1023:0] ms0;
    generate
        for (gs = 0; gs < 16; gs = gs + 1) begin : sel0
            assign ms0[gs*64 +: 64] = msg0[SIGMA[gs*4 +: 4]*64 +: 64];
        end
    endgenerate

    // Stages 1 to 11 apply rounds 1 to 11.
    wire [1023:0] msg  [1:11];
    wire [1023:0] ms   [1:11];
    wire [1023:0] vnext[1:11];
    generate
        for (gs = 1; gs <= 11; gs = gs + 1) begin : stage
            assign msg[gs] = {384'd0, fixed, n_s[gs]};
            for (gw = 0; gw < 16; gw = gw + 1) begin : sel
                assign ms[gs][gw*64 +: 64] =
                    msg[gs][SIGMA[(gs*16+gw)*4 +: 4]*64 +: 64];
            end
            assign vnext[gs] = round_fn(v_s[gs], ms[gs]);
        end
    endgenerate

    // The finished chaining value out of stage 12.
    wire [63:0] h0_out = H_IN[63:0] ^ v_s[12][63:0] ^ v_s[12][8*64 +: 64];
    wire [63:0] compare_value = {h0_out[7:0],   h0_out[15:8],  h0_out[23:16],
                                 h0_out[31:24], h0_out[39:32], h0_out[47:40],
                                 h0_out[55:48], h0_out[63:56]};

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 1; i <= 12; i = i + 1) v_s[i] <= 1024'd0;
            for (i = 1; i <= 12; i = i + 1) begin
                n_s[i]   <= 64'd0;
                val_s[i] <= 1'b0;
            end
            hit <= 1'b0; exhausted <= 1'b0; busy <= 1'b0; feeding <= 1'b0;
            hashes <= 64'd0; nonce <= 64'd0;
            hit_nonce <= 64'd0; hit_value <= 64'd0;
            fixed <= 576'd0; bound_r <= 64'd0; ceiling_r <= 64'd0;
        end else begin
            hit <= 1'b0;

            if (load) begin
                fixed     <= work_fixed;
                bound_r   <= bound;
                ceiling_r <= nonce_ceiling;
                nonce     <= nonce_start;
                feeding   <= 1'b1;
                busy      <= 1'b1;
                exhausted <= 1'b0;
                hashes    <= 64'd0;
                for (i = 1; i <= 12; i = i + 1) val_s[i] <= 1'b0;
            end else begin
                // Feed one nonce per cycle until the ceiling is past.
                v_s[1]   <= round_fn(V_INIT, ms0);
                n_s[1]   <= nonce;
                val_s[1] <= feeding;
                if (feeding) begin
                    if (nonce == ceiling_r)
                        feeding <= 1'b0;
                    else
                        nonce <= nonce + 64'd1;
                end

                for (i = 1; i <= 11; i = i + 1) begin
                    v_s[i+1]   <= vnext[i];
                    n_s[i+1]   <= n_s[i];
                    val_s[i+1] <= val_s[i];
                end

                if (val_s[12]) begin
                    hashes <= hashes + 64'd1;
                    if (compare_value <= bound_r) begin
                        hit       <= 1'b1;
                        hit_nonce <= n_s[12];
                        hit_value <= compare_value;
                    end
                    if (n_s[12] == ceiling_r) begin
                        exhausted <= 1'b1;
                        busy      <= 1'b0;
                    end
                end
            end
        end
    end

endmodule

`default_nettype wire
