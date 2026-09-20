// blake2b_defs.vh - the constants and the G function, shared by the cores.
// Include inside a module body.

    localparam [63:0] IV0 = 64'h6A09E667F3BCC908;
    localparam [63:0] IV1 = 64'hBB67AE8584CAA73B;
    localparam [63:0] IV2 = 64'h3C6EF372FE94F82B;
    localparam [63:0] IV3 = 64'hA54FF53A5F1D36F1;
    localparam [63:0] IV4 = 64'h510E527FADE682D1;
    localparam [63:0] IV5 = 64'h9B05688C2B3E6C1F;
    localparam [63:0] IV6 = 64'h1F83D9ABFB41BD6B;
    localparam [63:0] IV7 = 64'h5BE0CD19137E2179;

    // The message schedule, round r nibble i at SIGMA[(r*16+i)*4 +: 4].
    // Rounds 10 and 11 repeat rounds 0 and 1.
    localparam [767:0] SIGMA = {
        64'h357B20C16DF984AE, 64'hFEDCBA9876543210,   // 11, 10
        64'h0DC3E9BF5167482A, 64'h5A417D2C803B9EF6,   //  9,  8
        64'hA2684F05931CE7BD, 64'hB8293670A4DEF15C,   //  7,  6
        64'h91EF57D438B0A6C2, 64'hD386CB1EFA427509,   //  5,  4
        64'h8F04A562EBCD1397, 64'h491763EADF250C8B,   //  3,  2
        64'h357B20C16DF984AE, 64'hFEDCBA9876543210    //  1,  0
    };

    // One G. Returns {a, b, c, d} packed most significant first.
    function [255:0] g_fn;
        input [63:0] a, b, c, d, x, y;
        reg [63:0] va, vb, vc, vd;
        begin
            va = a + b + x;
            vd = d ^ va;  vd = {vd[31:0], vd[63:32]};   // rotate right 32
            vc = c + vd;
            vb = b ^ vc;  vb = {vb[23:0], vb[63:24]};   // rotate right 24
            va = va + vb + y;
            vd = vd ^ va; vd = {vd[15:0], vd[63:16]};   // rotate right 16
            vc = vc + vd;
            vb = vb ^ vc; vb = {vb[62:0], vb[63]};      // rotate right 63
            g_fn = {va, vb, vc, vd};
        end
    endfunction

    // One full round of v, given the sixteen message words already selected.
    function [1023:0] round_fn;
        input [1023:0] vin;
        input [1023:0] ms;          // ms[i] is already sigma-permuted
        reg [63:0] w [0:15];
        reg [255:0] q0, q1, q2, q3;
        integer j;
        begin
            for (j = 0; j < 16; j = j + 1)
                w[j] = vin[j*64 +: 64];

            q0 = g_fn(w[0], w[4], w[8],  w[12], ms[0*64 +: 64], ms[1*64 +: 64]);
            q1 = g_fn(w[1], w[5], w[9],  w[13], ms[2*64 +: 64], ms[3*64 +: 64]);
            q2 = g_fn(w[2], w[6], w[10], w[14], ms[4*64 +: 64], ms[5*64 +: 64]);
            q3 = g_fn(w[3], w[7], w[11], w[15], ms[6*64 +: 64], ms[7*64 +: 64]);

            w[0] = q0[255:192]; w[4] = q0[191:128]; w[8]  = q0[127:64]; w[12] = q0[63:0];
            w[1] = q1[255:192]; w[5] = q1[191:128]; w[9]  = q1[127:64]; w[13] = q1[63:0];
            w[2] = q2[255:192]; w[6] = q2[191:128]; w[10] = q2[127:64]; w[14] = q2[63:0];
            w[3] = q3[255:192]; w[7] = q3[191:128]; w[11] = q3[127:64]; w[15] = q3[63:0];

            q0 = g_fn(w[0], w[5], w[10], w[15], ms[8*64  +: 64], ms[9*64  +: 64]);
            q1 = g_fn(w[1], w[6], w[11], w[12], ms[10*64 +: 64], ms[11*64 +: 64]);
            q2 = g_fn(w[2], w[7], w[8],  w[13], ms[12*64 +: 64], ms[13*64 +: 64]);
            q3 = g_fn(w[3], w[4], w[9],  w[14], ms[14*64 +: 64], ms[15*64 +: 64]);

            w[0] = q0[255:192]; w[5] = q0[191:128]; w[10] = q0[127:64]; w[15] = q0[63:0];
            w[1] = q1[255:192]; w[6] = q1[191:128]; w[11] = q1[127:64]; w[12] = q1[63:0];
            w[2] = q2[255:192]; w[7] = q2[191:128]; w[8]  = q2[127:64]; w[13] = q2[63:0];
            w[3] = q3[255:192]; w[4] = q3[191:128]; w[9]  = q3[127:64]; w[14] = q3[63:0];

            for (j = 0; j < 16; j = j + 1)
                round_fn[j*64 +: 64] = w[j];
        end
    endfunction
