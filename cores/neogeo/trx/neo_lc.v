// T143 §3b: a small fully associative cache of 128 bit lines of the DDR2 (ui_clk), for neo_mem.v. Written for this project (TREX), no upstream code.
// One line = the 16 bytes one DDR2 command returns. A line is invalid (0), wanted (1: allocated, not yet asked of the DDR2), in flight (2) or valid (3).
// One "ensure present" operation per clock: a hit changes nothing (an urgent operation raises a wanted line), a miss takes a victim (round robin among the lines that are
// neither wanted nor in flight nor protected) and makes it wanted. The owner takes the wanted lines with iss_* / iss_take (urgent ones first), and hands the line back with
// fill_*; the data RAM is written by the fill only and read asynchronously (distributed RAM). `flush` drops everything (the ROM may have been written meanwhile).
`timescale 1ns / 1ps
module neo_lc #(
    parameter N = 8,
    parameter IW = 3
) (
    input  wire          clk,
    input  wire          flush,
    input  wire          op_v,                  // ensure that line op_la is present (this clock)
    input  wire [22:0]   op_la,
    input  wire          op_urg,
    input  wire          op_alloc_ok,           // 0: the operation waits (a hit is not served either)
    output wire          op_done,               // hit, or a victim was taken
    output wire [IW-1:0] op_idx,
    output wire [1:0]    op_cls,                // 0 hit on a valid line, 1 hit on a line that is wanted / in flight, 2 miss
    input  wire          prot_v,                // a line somebody waits for: never a victim
    input  wire [IW-1:0] prot_idx,
    output wire          iss_v,
    output wire          iss_urg,
    output wire [IW-1:0] iss_idx,
    output wire [22:0]   iss_la,
    input  wire          iss_take,
    input  wire          fill_v,
    input  wire [IW-1:0] fill_idx,
    input  wire [127:0]  fill_data,
    input  wire [IW-1:0] rd_idx,
    output wire [127:0]  rd_data,
    output wire          rd_valid
);
    reg [22:0] tag [0:N-1];
    reg [1:0]  st  [0:N-1];
    reg        urg [0:N-1];
    reg [127:0] dat [0:N-1];
    reg [IW-1:0] rr = {IW{1'b0}};
    integer i;
    initial for (i = 0; i < N; i = i + 1) begin st[i] = 2'd0; urg[i] = 1'b0; tag[i] = 23'd0; end

    reg m_any;
    reg [IW-1:0] m_idx;
    reg v_ok;
    reg [IW-1:0] v_idx, vc;
    reg s_any, u_any;
    reg [IW-1:0] s_idx, u_idx;
    always @* begin
        m_any = 1'b0; m_idx = {IW{1'b0}};
        v_ok = 1'b0; v_idx = {IW{1'b0}};
        s_any = 1'b0; s_idx = {IW{1'b0}}; u_any = 1'b0; u_idx = {IW{1'b0}};
        vc = {IW{1'b0}};
        for (i = N - 1; i >= 0; i = i - 1) begin
            if (st[i] != 2'd0 && tag[i] == op_la) begin m_any = 1'b1; m_idx = i[IW-1:0]; end
            vc = rr + i[IW-1:0];
            if ((st[vc] == 2'd0 || st[vc] == 2'd3) && !(prot_v && prot_idx == vc)) begin v_ok = 1'b1; v_idx = vc; end
            if (st[i] == 2'd1) begin s_any = 1'b1; s_idx = i[IW-1:0]; if (urg[i]) begin u_any = 1'b1; u_idx = i[IW-1:0]; end end
        end
    end
    assign op_done = op_v && op_alloc_ok && (m_any || v_ok);
    assign op_idx  = m_any ? m_idx : v_idx;
    assign op_cls  = m_any ? (st[m_idx] == 2'd3 ? 2'd0 : 2'd1) : 2'd2;
    assign iss_v   = s_any;
    assign iss_urg = u_any;
    assign iss_idx = u_any ? u_idx : s_idx;
    assign iss_la  = tag[iss_idx];
    assign rd_data = dat[rd_idx];
    assign rd_valid = (st[rd_idx] == 2'd3);

    always @(posedge clk) begin
        if (flush) begin
            for (i = 0; i < N; i = i + 1) st[i] <= 2'd0;
        end else begin
            if (op_done) begin
                if (m_any) begin
                    if (op_urg && st[m_idx] == 2'd1) urg[m_idx] <= 1'b1;
                end else begin
                    tag[v_idx] <= op_la; st[v_idx] <= 2'd1; urg[v_idx] <= op_urg; rr <= v_idx + 1'b1;
                end
            end
            if (iss_take) st[iss_idx] <= 2'd2;
            if (fill_v && st[fill_idx] == 2'd2) st[fill_idx] <= 2'd3;
        end
        if (fill_v) dat[fill_idx] <= fill_data;
    end
endmodule
