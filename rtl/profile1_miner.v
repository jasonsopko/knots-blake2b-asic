// profile1_miner.v - a Bitcoin Knots proof-of-work profile 1 search engine.
//
// Holds one work item, walks the nonce, reports every value whose digest is at
// or under the bound. See docs/profile-1.md.
//
// One core, one hash at a time: sixteen cycles per attempt, which is twelve
// rounds plus the load, the start, the finish and the compare. That is a
// reference, not a product. A part replicates the core and pipelines it.

`default_nettype none

module profile1_miner (
    input  wire        clk,
    input  wire        rst_n,

    // Work. Hold stable while the engine runs.
    input  wire        load,            // one cycle
    input  wire [575:0] work_fixed,     // m[1..9]: bytes 0x08..0x4F
    input  wire [63:0] bound,
    input  wire [63:0] nonce_start,
    input  wire [63:0] nonce_ceiling,   // last value tried

    output reg         hit,             // one cycle per result
    output reg  [63:0] hit_nonce,
    output reg  [63:0] hit_value,       // the compared 64 bits, for the host
    output reg         exhausted,
    output reg         busy,
    output reg  [63:0] hashes
);

    // BLAKE2b parameter block for an unkeyed 32 byte digest: h[0] carries
    // 0x01010020, which is fanout 1, depth 1, key length 0, digest length 32.
    localparam [63:0] H0 = 64'h6A09E667F3BCC908 ^ 64'h0000000001010020;
    localparam [511:0] H_IN = {
        64'h5BE0CD19137E2179, 64'h1F83D9ABFB41BD6B,
        64'h9B05688C2B3E6C1F, 64'h510E527FADE682D1,
        64'hA54FF53A5F1D36F1, 64'h3C6EF372FE94F82B,
        64'hBB67AE8584CAA73B, H0
    };

    // 80 bytes of message in one padded block.
    localparam [63:0] MSG_BYTES = 64'd80;

    reg  [63:0]  nonce;
    reg  [575:0] fixed;
    reg  [63:0]  bound_r, ceiling_r;

    // m[0] is the nonce, m[1..9] the work, m[10..15] the zero padding.
    wire [1023:0] m = {384'd0, fixed, nonce};

    reg          core_start;
    wire [511:0] core_h_out;
    wire         core_done, core_busy;

    blake2b_core core (
        .clk         (clk),
        .rst_n       (rst_n),
        .start       (core_start),
        .m           (m),
        .t           (MSG_BYTES),
        .final_block (1'b1),
        .h_in        (H_IN),
        .h_out       (core_h_out),
        .done        (core_done),
        .busy        (core_busy)
    );

    // The chip compares the digest's leading 64 bits as a big-endian number,
    // which is h[0] with its bytes swapped.
    wire [63:0] h0 = core_h_out[63:0];
    wire [63:0] compare_value = {h0[7:0],   h0[15:8],  h0[23:16], h0[31:24],
                                 h0[39:32], h0[47:40], h0[55:48], h0[63:56]};

    localparam [1:0] S_IDLE = 2'd0, S_RUN = 2'd1, S_CHECK = 2'd2;
    reg [1:0] state;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state      <= S_IDLE;
            core_start <= 1'b0;
            hit        <= 1'b0;
            exhausted  <= 1'b0;
            busy       <= 1'b0;
            hashes     <= 64'd0;
            nonce      <= 64'd0;
            hit_nonce  <= 64'd0;
            hit_value  <= 64'd0;
            fixed      <= 576'd0;
            bound_r    <= 64'd0;
            ceiling_r  <= 64'd0;
        end else begin
            hit        <= 1'b0;
            core_start <= 1'b0;

            case (state)
            S_IDLE: begin
                if (load) begin
                    fixed     <= work_fixed;
                    bound_r   <= bound;
                    ceiling_r <= nonce_ceiling;
                    nonce     <= nonce_start;
                    exhausted <= 1'b0;
                    hashes    <= 64'd0;
                    busy      <= 1'b1;
                    state     <= S_RUN;
                end
            end

            S_RUN: begin
                // done and busy both move on the cycle the core finishes, so
                // done has to be tested first. Testing busy first restarts the
                // core on that cycle and the result is never looked at, which
                // grinds forever and reports nothing.
                //
                // The message has to sit still while the core chews on it, so
                // the nonce moves in S_CHECK, not here.
                if (core_done) begin
                    state  <= S_CHECK;
                    hashes <= hashes + 64'd1;
                end else if (!core_busy && !core_start) begin
                    core_start <= 1'b1;
                end
            end

            S_CHECK: begin
                if (compare_value <= bound_r) begin
                    hit       <= 1'b1;
                    hit_nonce <= nonce;
                    hit_value <= compare_value;
                end

                if (nonce == ceiling_r) begin
                    exhausted <= 1'b1;
                    busy      <= 1'b0;
                    state     <= S_IDLE;
                end else begin
                    nonce <= nonce + 64'd1;
                    state <= S_RUN;
                end
            end

            default: state <= S_IDLE;
            endcase
        end
    end

endmodule

`default_nettype wire
