// A tap on the RISC-V (T20 bug hunt: a hung application cannot be located
// otherwise, the core has no debug port). All in cpu_clk; trxdbg.sh rvpc
// reads it over JTAG (top.v, dbg_addr 0x100040-0x100047).
//
// tap (ultimate_sys.vhd rv_tap): [31:0] PC of the instruction the execute stage
// handed on last, [32] the CPU waits on an I/O access, [33] memory request
// pending, [34] it reads, [35] IRQ line, [43:36] request tag, [69:44] memory
// address, [93:70] I/O address, [94] I/O write, [103:96] DMA timeouts.
//
// A snapshot (toggle snap_req) freezes eight words until the next one:
//   0  PC
//   1  tap[63:32]: I/O busy, memory pending, read, IRQ line, tag, memory address 19:0
//   2  tap[95:64]: memory address 25:20, I/O address, I/O write
//   3  {b31:24 app DMA bytes the C64 side never answered (ultimate_sys.vhd gives up after 2^23
//      clocks, acks $FF), b23:0 IRQ line rising edges since reset}
//   4  clocks the PC has stood still, running (saturates)
//   5  the longest such run since reset or clr_req
//   6  the PC during that run
//   7  {b27 I/O write, b26 read, b25 memory pending, b24 I/O busy,
//      b23:0 I/O address} as they stood while that run grew
// A hung CPU shows a PC that stands still with a run count that keeps growing;
// "I/O busy" or "memory pending" says what it waits for, the address which.
module rv_dbg (
    input  wire         clk,
    input  wire         rst,
    input  wire [127:0] tap,
    input  wire         snap_req,       // toggles (from another clock)
    input  wire         clr_req,        // toggles
    output reg  [255:0] hold = 256'd0
);
    (* ASYNC_REG = "TRUE" *) reg [1:0] snap_s = 2'b00, clr_s = 2'b00;
    reg         snap_seen = 1'b0, clr_seen = 1'b0, irq_d = 1'b0;
    reg  [31:0] pc_d = 32'd0, run = 32'd0, maxrun = 32'd0, maxpc = 32'd0, edges = 32'd0;
    reg  [31:0] maxctx = 32'd0;
    wire [31:0] ctx = {4'd0, tap[94], tap[34], tap[33], tap[32], tap[93:70]};
    always @(posedge clk) begin
        snap_s <= {snap_s[0], snap_req};
        clr_s  <= {clr_s[0], clr_req};
        pc_d   <= tap[31:0];
        irq_d  <= tap[35];
        if (tap[35] && !irq_d) edges <= edges + 32'd1;
        if (tap[31:0] != pc_d) run <= 32'd0;
        else if (run != 32'hFFFFFFFF) run <= run + 32'd1;
        if (run > maxrun) begin
            maxrun <= run;
            maxpc  <= pc_d;
            maxctx <= ctx;
        end
        if (snap_s[1] != snap_seen) begin
            snap_seen <= snap_s[1];
            hold <= {maxctx, maxpc, maxrun, run, tap[103:96], edges[23:0], tap[95:64], tap[63:32], tap[31:0]};
        end
        if (clr_s[1] != clr_seen || rst) begin
            clr_seen <= clr_s[1];
            maxrun <= 32'd0;
        end
    end
endmodule
