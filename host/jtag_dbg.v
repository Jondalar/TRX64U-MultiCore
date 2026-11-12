// Debug access over JTAG (USER1, BSCANE2): read and write a 24-bit address
// space from the Mac (tools/trxdbg.py) while the design runs.
//
// One 64-bit data register. What is shifted in is a command; what is
// shifted out is the result of the previous one:
//   in:  [63:62] op (0 none, 1 read, 2 write, 3 write 4 bytes: data[7:0] at
//        address, data[15:8] at address+1, ...; for byte targets such as the
//        C64 RAM, so that a bulk load moves four bytes per scan),
//        [55:32] address, [31:0] write data
//   out: [63:62] op of the last command, [61:56] command count (6 bits),
//        [55:32] its address, [31:0] read data
// A command is taken on UPDATE-DR and carried out in clk; the result is ready
// long before the next scan's CAPTURE-DR (TCK is far slower than clk, and
// slow targets such as the DDR2 answer within a microsecond).
module jtag_dbg (
    input  wire        clk,
    output reg  [23:0] addr,
    output reg  [31:0] wdata,
    output reg         re,              // one clk pulse
    output reg         we,              // one clk pulse (op 3: four, one per byte)
    input  wire [31:0] rdata,           // read data, taken when rvalid
    input  wire        rvalid           // high for the clk rdata is valid (any time after re)
);
    wire tck, tdi, capture, shift, update, sel;
    reg [63:0] sr;
    BSCANE2 #(.JTAG_CHAIN(1)) bscan (
        .CAPTURE(capture), .DRCK(), .RESET(), .RUNTEST(), .SEL(sel), .SHIFT(shift),
        .TCK(tck), .TDI(tdi), .UPDATE(update), .TMS(), .TDO(sr[0])
    );

    // ---- TCK side ------------------------------------------------------------
    reg [63:0] cmd;
    reg        cmd_tgl = 1'b0;
    reg [63:0] result;                  // written in clk, stable when captured
    always @(posedge tck) begin
        if (sel && capture) sr <= result;
        else if (sel && shift) sr <= {tdi, sr[63:1]};
    end
    // TDO straight from the register: the TAP itself retimes it on the
    // falling edge (an own negedge flop here shifted every result by a bit).
    always @(posedge tck)
        if (sel && update) begin
            cmd     <= sr;
            cmd_tgl <= ~cmd_tgl;
        end

    // ---- clk side ------------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [1:0] tgl_s = 2'b00;
    reg tgl_seen = 1'b0;                // a command is new while tgl_s[1] differs
    reg [1:0] op;
    reg       rd_pending = 1'b0;
    reg [1:0] burst = 2'd0;             // op 3: bytes still to write after this one
    reg [5:0] count = 6'd0;
    always @(posedge clk) begin
        tgl_s <= {tgl_s[0], cmd_tgl};
        re <= 1'b0;
        we <= 1'b0;
        if (burst != 2'd0) begin
            burst <= burst - 2'd1;
            addr  <= addr + 24'd1;
            wdata <= {8'd0, wdata[31:8]};
            we    <= 1'b1;
        end else if (tgl_s[1] != tgl_seen) begin
            tgl_seen <= tgl_s[1];
            op    <= cmd[63:62];
            addr  <= cmd[55:32];
            wdata <= cmd[31:0];
            re    <= (cmd[63:62] == 2'd1);
            rd_pending <= (cmd[63:62] == 2'd1);
            we    <= cmd[63];
            burst <= (cmd[63:62] == 2'd3) ? 2'd3 : 2'd0;
            count <= count + 6'd1;
            if (cmd[63:62] != 2'd1)
                result <= {cmd[63:62], count + 6'd1, cmd[55:32], 32'd0};
        end
        if (rd_pending && rvalid) begin
            result <= {op, count, addr, rdata};
            rd_pending <= 1'b0;
        end
    end
endmodule
