// fpga_top.v - a pin-count-sane wrapper so the engine can go on a board.
//
// Work goes in and results come out one bit at a time, because a parallel
// interface would want a thousand pins and no package has them. Eight signals
// total. Set PIPELINED to pick which engine is inside.

`default_nettype none

module fpga_top #(
    parameter PIPELINED = 1
) (
    input  wire clk,
    input  wire rst_n,

    input  wire sin,            // work bits, most significant first
    input  wire shift_en,       // one bit per clock while high
    input  wire start,          // pulse once the work is in

    output wire sout,           // result bits, most significant first
    output wire hit_o,
    output wire busy_o
);

    localparam WORK_BITS   = 576 + 64 + 64 + 64;   // fixed, bound, start, ceiling
    localparam RESULT_BITS = 64 + 64;              // nonce, value

    reg [WORK_BITS-1:0]   work_sr;
    reg [RESULT_BITS-1:0] result_sr;

    wire        hit;
    wire [63:0] hit_nonce, hit_value, hashes;
    wire        exhausted, busy;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            work_sr   <= {WORK_BITS{1'b0}};
            result_sr <= {RESULT_BITS{1'b0}};
        end else begin
            if (shift_en)
                work_sr <= {work_sr[WORK_BITS-2:0], sin};

            if (hit)
                result_sr <= {hit_nonce, hit_value};
            else if (shift_en)
                result_sr <= {result_sr[RESULT_BITS-2:0], 1'b0};
        end
    end

    assign sout   = result_sr[RESULT_BITS-1];
    assign hit_o  = hit;
    assign busy_o = busy;

    generate
        if (PIPELINED) begin : eng
            profile1_miner_pipe u_eng (
                .clk(clk), .rst_n(rst_n), .load(start),
                .work_fixed   (work_sr[WORK_BITS-1 -: 576]),
                .bound        (work_sr[191:128]),
                .nonce_start  (work_sr[127:64]),
                .nonce_ceiling(work_sr[63:0]),
                .hit(hit), .hit_nonce(hit_nonce), .hit_value(hit_value),
                .exhausted(exhausted), .busy(busy), .hashes(hashes)
            );
        end else begin : eng
            profile1_miner u_eng (
                .clk(clk), .rst_n(rst_n), .load(start),
                .work_fixed   (work_sr[WORK_BITS-1 -: 576]),
                .bound        (work_sr[191:128]),
                .nonce_start  (work_sr[127:64]),
                .nonce_ceiling(work_sr[63:0]),
                .hit(hit), .hit_nonce(hit_nonce), .hit_value(hit_value),
                .exhausted(exhausted), .busy(busy), .hashes(hashes)
            );
        end
    endgenerate

endmodule

`default_nettype wire
