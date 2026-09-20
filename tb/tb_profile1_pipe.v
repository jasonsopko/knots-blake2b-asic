// Loads one work item whose answer the model already knows, and checks the
// engine finds the same nonce.
`timescale 1ns/1ps

module tb_profile1_pipe;

    reg clk = 0, rst_n = 0, load = 0;
    reg [575:0] work_fixed;
    reg [63:0]  bound, nonce_start, nonce_ceiling;

    wire        hit, exhausted, busy;
    wire [63:0] hit_nonce, hit_value, hashes;

    reg [63:0] vec [0:11];        // m[1..9], bound, start, expected nonce
    reg [63:0] expected;
    integer i, hits, fails;
    integer cycles;

    profile1_miner_pipe dut (
        .clk(clk), .rst_n(rst_n), .load(load),
        .work_fixed(work_fixed), .bound(bound),
        .nonce_start(nonce_start), .nonce_ceiling(nonce_ceiling),
        .hit(hit), .hit_nonce(hit_nonce), .hit_value(hit_value),
        .exhausted(exhausted), .busy(busy), .hashes(hashes)
    );

    always #5 clk = ~clk;

    always @(posedge clk) begin
        if (busy)
            cycles = cycles + 1;
        if (hit) begin
            hits = hits + 1;
            $display("  hit at nonce %0d, value %016x", hit_nonce, hit_value);
            if (hit_nonce !== expected) begin
                $display("  FAIL: model said nonce %0d", expected);
                fails = fails + 1;
            end
            if (hit_value > bound) begin
                $display("  FAIL: reported a value above the bound");
                fails = fails + 1;
            end
        end
    end

    initial begin
        $readmemh("tb/miner_vector.mem", vec);
        hits = 0; fails = 0; cycles = 0;

        for (i = 0; i < 9; i = i + 1)
            work_fixed[i*64 +: 64] = vec[i];
        bound         = vec[9];
        nonce_start   = vec[10];
        expected      = vec[11];
        nonce_ceiling = expected;      // stop on the answer

        @(posedge clk); rst_n = 1;
        @(negedge clk); load = 1;
        @(negedge clk); load = 0;

        wait (exhausted);
        repeat (3) @(posedge clk);     // let the hit monitor settle first

        $display("  searched %0d nonces in %0d cycles, %0d hit(s)",
                 hashes, cycles, hits);
        $display("  pipeline: one hash per cycle after a 12 stage fill");
        if (hits != 1) begin
            $display("  FAIL: expected exactly one hit");
            fails = fails + 1;
        end

        if (fails == 0)
            $display("profile1_miner_pipe: found the nonce the model found");
        else
            $display("profile1_miner_pipe: %0d FAILURES", fails);
        $finish;
    end

    initial begin
        if ($test$plusargs("vcd")) begin
            $dumpfile("build/tb_pipe.vcd");
            $dumpvars(0, tb_profile1_pipe);
        end
    end

    initial begin
        #2000000;
        $display("profile1_miner_pipe: TIMEOUT");
        $finish;
    end

endmodule
