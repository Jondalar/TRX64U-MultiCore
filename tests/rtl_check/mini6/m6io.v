// The bus side of the miniature handshake: a request to 0x20-0x3F of the device page is held and handed over by a toggle.
module m6io (
    input  wire       clk,
    input  wire       we,
    input  wire       re,
    input  wire [7:0] addr,
    input  wire [7:0] wdata,
    output wire [7:0] rdata,
    output reg        rq_tgl = 1'b0,
    output reg  [7:0] rq_addr = 8'd0,
    output reg        rq_we = 1'b0,
    output reg  [7:0] rq_wdata = 8'd0,
    input  wire       rs_tgl,
    input  wire [7:0] rs_rdata
);
    always @(posedge clk)
        if (we || re) begin
            rq_addr  <= addr;
            rq_we    <= we;
            rq_wdata <= wdata;
            rq_tgl   <= ~rq_tgl;
        end
    assign rdata = rs_rdata;
endmodule
