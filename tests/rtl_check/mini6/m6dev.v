// The module behind the continuous assignments of m6pipe (trx_engine.v behind trx_sys.v in miniature). It decodes nothing itself: it hands the strobe, the address and the data
// on to the unit it holds (c64_core.v holds trx_dbg.v).
module m6dev (
    input  wire       clk,
    input  wire       we,
    input  wire [7:0] addr,
    input  wire [7:0] wdata
);
    m6unit u (.clk(clk), .we(we), .addr(addr), .wdata(wdata));
endmodule
