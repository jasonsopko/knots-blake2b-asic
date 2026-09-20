// Drives the compression core with the golden vectors from the model.
`timescale 1ns/1ps

module tb_blake2b_core;

    localparam NVEC = 8;
    localparam WORDS_PER_VEC = 20;      // m[0..15] then the four digest words

    reg clk = 0, rst_n = 0, start = 0;
    reg [1023:0] m;
    reg [511:0]  h_in;
    wire [511:0] h_out;
    wire done, busy;

    integer vec, i, fails;

    reg [63:0] mem [0:NVEC*WORDS_PER_VEC-1];

    // Unkeyed, 32 byte digest.
    localparam [511:0] H_IN_32 = {
        64'h5BE0CD19137E2179, 64'h1F83D9ABFB41BD6B,
        64'h9B05688C2B3E6C1F, 64'h510E527FADE682D1,
        64'hA54FF53A5F1D36F1, 64'h3C6EF372FE94F82B,
        64'hBB67AE8584CAA73B, 64'h6A09E667F3BCC908 ^ 64'h0000000001010020
    };

    blake2b_core dut (
        .clk(clk), .rst_n(rst_n), .start(start),
        .m(m), .t(64'd80), .final_block(1'b1), .h_in(h_in),
        .h_out(h_out), .done(done), .busy(busy)
    );

    always #5 clk = ~clk;

    initial begin
        if ($test$plusargs("vcd")) begin
            $dumpfile("build/tb_core.vcd");
            $dumpvars(0, tb_blake2b_core);
        end
    end

    initial begin
        $readmemh("tb/core_vectors.mem", mem);
        h_in = H_IN_32;
        fails = 0;

        @(posedge clk); rst_n = 1;
        @(posedge clk);

        for (vec = 0; vec < NVEC; vec = vec + 1) begin
            for (i = 0; i < 16; i = i + 1)
                m[i*64 +: 64] = mem[vec*WORDS_PER_VEC + i];

            @(negedge clk);
            start = 1;
            @(negedge clk);
            start = 0;

            wait (done);
            @(negedge clk);

            for (i = 0; i < 4; i = i + 1) begin
                if (h_out[i*64 +: 64] !== mem[vec*WORDS_PER_VEC + 16 + i]) begin
                    $display("  FAIL vector %0d word %0d: got %016x want %016x",
                             vec, i, h_out[i*64 +: 64], mem[vec*WORDS_PER_VEC+16+i]);
                    fails = fails + 1;
                end
            end
        end

        if (fails == 0)
            $display("blake2b_core: %0d vectors, all match the model", NVEC);
        else
            $display("blake2b_core: %0d FAILURES", fails);
        $finish;
    end

endmodule
