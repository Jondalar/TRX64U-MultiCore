// one access at a time on a mig_arb toggle port, random addresses from BASE,
// WE_PCT percent writes, a random gap; latency up to the done toggle
module tgl_client #(
    parameter [31:0] SEED = 1,
    parameter [25:0] BASE = 0,
    parameter        WE_PCT = 50,
    parameter        GAP = 0            // clocks between accesses, + 0..15
) (
    input  wire        clk,
    input  wire        en,
    output reg         req_tgl = 0,
    output reg  [25:0] addr = 0,
    output reg         we = 0,
    output reg  [31:0] wdata = 0,
    output reg  [3:0]  be = 4'hF,
    input  wire        done_tgl
);
    reg [31:0] rnd = SEED;
    always @(posedge clk) rnd <= {rnd[30:0], rnd[31] ^ rnd[21] ^ rnd[1] ^ rnd[0]};
    reg [1:0] s = 0;
    always @(posedge clk) s <= {s[0], done_tgl};
    reg open = 0; integer gap = 0; time t0, lat_max = 0;
    always @(posedge clk) begin
        if (!open && en && gap == 0) begin
            open <= 1; t0 = $time;
            addr <= BASE | {8'd0, rnd[17:2], 2'b00};
            we <= (rnd[31:25] % 100) < WE_PCT; wdata <= rnd;
            req_tgl <= ~req_tgl;
        end else if (open && s[1] == req_tgl) begin
            open <= 0; gap = GAP + rnd[3:0];
            if ($time - t0 > lat_max) lat_max = $time - t0;
        end else if (gap) gap = gap - 1;
    end
endmodule
