// (mini6, V-1f: as mini3, with a register that a write clears whatever the data is (clr, 0x50: trx_sys.v sb_err) and a unit two modules behind.)
// The clock-domain side of the miniature handshake (trx_sys.v in miniature): the request goes through two registers (c_addr, then op_a) before the
// decode, another master (j_addr) loads op_a too, two cells are made by a generate-for with a named block, and a function swaps the nibbles of a read.
module m6pipe (
    input  wire       clk,
    input  wire       rq_tgl,
    input  wire [7:0] rq_addr,
    input  wire       rq_we,
    input  wire [7:0] rq_wdata,
    output reg        rs_tgl = 1'b0,
    output reg  [7:0] rs_rdata = 8'd0,
    input  wire [7:0] j_addr,
    input  wire       j_we,
    output wire       d_we,
    output wire [7:0] d_addr,
    output wire [7:0] d_wdata
);
    reg  [2:0] rq_s = 3'b000;
    reg        c_pend = 1'b0;
    reg  [7:0] c_addr;
    reg        c_we;
    reg  [7:0] c_wd;
    reg  [7:0] op_a;
    reg        op_we;
    reg  [7:0] op_wd;
    localparam [1:0] O_IDLE = 2'd0, O_RUN = 2'd1;
    reg  [1:0] ost = O_IDLE;
    reg  [7:0] r0 = 8'd0;
    reg        clr = 1'b1;
    function [7:0] swz(input [7:0] a);
        swz = {a[3:0], a[7:4]};
    endfunction
    // the request goes on to m6dev over continuous assignments (trx_sys.v: p2_we = ost == O_RUN && op_we && page, p2_addr, p2_wdata)
    assign d_we    = ost == O_RUN && op_we && op_a[7];
    assign d_addr  = op_a;
    assign d_wdata = op_wd;
    wire rq_ev = rq_s[2] != rq_s[1];
    genvar gi;
    generate
        for (gi = 0; gi < 2; gi = gi + 1) begin : cell
            reg [7:0] v = 8'd0;
            always @(posedge clk)
                if (ost == O_RUN && op_we && op_a == 8'h30 + gi) v <= op_wd;
        end
    endgenerate
    reg  [7:0] rv;
    always @(*) begin
        rv = 8'd0;
        case (op_a)
        8'h20: rv = r0;
        8'h21: rv = 8'hC3;
        8'h22: rv = swz(r0);
        8'h50: rv = {7'd0, clr};
        8'h30: rv = cell[0].v;
        8'h31: rv = cell[1].v;
        default: ;
        endcase
    end
    always @(posedge clk) begin
        rq_s <= {rq_s[1:0], rq_tgl};
        if (rq_ev) begin
            c_addr <= rq_addr; c_we <= rq_we; c_wd <= rq_wdata;
            c_pend <= 1'b1;
        end
        case (ost)
        O_IDLE:
            if (c_pend) begin
                op_we <= c_we; op_a <= c_addr; op_wd <= c_wd;
                c_pend <= 1'b0;
                ost <= O_RUN;
            end else if (j_we) begin
                op_we <= 1'b1; op_a <= j_addr; op_wd <= 8'd0;
                ost <= O_RUN;
            end
        O_RUN: begin
            if (op_we) begin
                case (op_a)
                8'h20: r0 <= op_wd;
                8'h50: clr <= 1'b0;
                default: ;
                endcase
            end else
                rs_rdata <= rv;
            rs_tgl <= ~rs_tgl;
            ost <= O_IDLE;
        end
        default: ost <= O_IDLE;
        endcase
    end
endmodule
