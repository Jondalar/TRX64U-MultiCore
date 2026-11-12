// T116 S3: the reference of the equivalence bench: the Next's memory as the original has it, a one-cycle SRAM. Same name and ports as
// cores/zxnext/trx/zxn_mem.v (the bench build substitutes this file for that one), no cache, no wait, no DDR2 port: port A reads combinationally and
// writes one clock after the request starts (as zxn_mem captures it), port B reads combinationally. The 2 MB live in the DDR2 model of the bench
// (tb_zxn.cm.mem, a hierarchical name, so that the bench compares the two runs' memory with the same code). The SD buffer's lines are moved to and
// from the model's staging sector the same way. Sim only.
`timescale 1ns / 1ps
module zxn_mem (
    input  wire         clk,
    input  wire         rst,
    input  wire [31:0]  ram_base,
    input  wire [20:0]  a_addr,
    input  wire         a_req_t,
    input  wire         a_rd_n,
    input  wire [7:0]   a_do,
    output wire [7:0]   a_di,
    input  wire [20:0]  b_addr,
    input  wire         b_req_t,
    output wire [7:0]   b_di,
    output wire         stall,
    input  wire         sd_start,
    input  wire         sd_to_ddr,
    input  wire [31:0]  sd_stage,
    output reg          sd_done = 1'b0,
    output reg  [4:0]   sd_k = 5'd0,
    output reg          sd_we = 1'b0,
    output reg  [127:0] sd_wdata = 128'd0,
    input  wire [127:0] sd_rdata,
    output wire         busy,
    input  wire         ui_clk,
    input  wire         ui_rst,
    output wire [26:0]  c_addr,
    output wire [2:0]   c_cmd,
    output wire         c_en,
    input  wire         c_rdy,
    output wire [127:0] c_wdf_data,
    output wire [15:0]  c_wdf_mask,
    output wire         c_wdf_wren,
    input  wire [127:0] c_rd_data,
    input  wire         c_rd_valid,
    output reg  [31:0]  st_miss_a = 32'd0,
    output reg  [31:0]  st_miss_b = 32'd0,
    output reg  [31:0]  st_pref = 32'd0,
    output reg  [31:0]  st_stall = 32'd0,
    output reg  [31:0]  st_late = 32'd0
);
    assign c_addr = 27'd0;
    assign c_cmd = 3'd0;
    assign c_en = 1'b0;
    assign c_wdf_data = 128'd0;
    assign c_wdf_mask = 16'hFFFF;
    assign c_wdf_wren = 1'b0;
    assign stall = rst;
    assign busy = sdj;

    wire [127:0] aw = tb_zxn.cm.mem[a_addr >> 4];
    wire [127:0] bw = tb_zxn.cm.mem[b_addr >> 4];
    assign a_di = aw[8 * a_addr[3:0] +: 8];
    assign b_di = bw[8 * b_addr[3:0] +: 8];
    reg        a_tq = 1'b0, a_go = 1'b0, crd_q = 1'b1;
    reg [20:0] ca_q = 21'd0;
    reg [7:0]  cd_q = 8'd0;
    reg [127:0] w;
    always @(posedge clk) begin
        a_tq <= a_req_t;
        ca_q <= a_addr; cd_q <= a_do; crd_q <= ~a_rd_n;
        a_go <= a_req_t & ~a_tq & ~rst;
        if (a_go && !crd_q) begin
            w = tb_zxn.cm.mem[ca_q >> 4];
            w[8 * ca_q[3:0] +: 8] = cd_q;
            tb_zxn.cm.mem[ca_q >> 4] = w;
        end
    end

    // the SD job: 32 lines of the staging sector (DDR2 byte address sd_stage, its words at (sd_stage - base) / 16 of the model)
    reg        sdj = 1'b0, sdj_dir = 1'b0;
    reg [31:0] sdj_base = 32'd0;
    reg [5:0]  sdw = 6'd0;
    wire [18:0] widx = ((sdj_base - 32'h01000000) >> 4) + {14'd0, sd_k};
    always @(posedge clk) begin
        sd_done <= 1'b0; sd_we <= 1'b0;
        if (sd_start && !sdj) begin sdj <= 1'b1; sdj_dir <= sd_to_ddr; sdj_base <= sd_stage; sd_k <= 5'd0; sdw <= 6'd0; end
        else if (sdj) begin
            sdw <= sdw + 6'd1;
            if (sdw == 6'd2 && !sdj_dir) begin sd_wdata <= tb_zxn.cm.mem[widx]; sd_we <= 1'b1; end
            if (sdw == 6'd24 && sdj_dir) tb_zxn.cm.mem[widx] = sd_rdata;
            if (sdw == 6'd26) begin
                sdw <= 6'd0;
                if (sd_k == 5'd31) begin sd_done <= 1'b1; sdj <= 1'b0; sd_k <= 5'd0; end
                else sd_k <= sd_k + 5'd1;
            end
        end
    end
endmodule
