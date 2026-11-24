// The unit two modules behind the handshake (trx_dbg.v in miniature): two cells that a generate-for makes, the address of each compared with a select of the genvar ({6'h2A, gu[0]}),
// a register written from the data under a compare of the address (own, at 0xB4), and a status bit that only takes the data when its own address is written (ev: the data of the
// compare behind a false address is no write of another address).
module m6unit (
    input  wire       clk,
    input  wire       we,
    input  wire [7:0] addr,
    input  wire [7:0] wdata
);
    wire wcmd = we && addr == 8'hB0;
    reg  own = 1'b0, ev = 1'b0, busy = 1'b0;
    genvar gu;
    generate
        for (gu = 0; gu < 2; gu = gu + 1) begin : cellu
            reg [7:0] q = 8'd0;
            wire wu = we && addr[7:1] == {6'h2A, gu[0]};
            always @(posedge clk)
                if (wu) q <= wdata;
        end
    endgenerate
    always @(posedge clk) begin
        ev <= busy || (wcmd && wdata == 8'h01);
        if (we && addr == 8'hB4) own <= wdata[0];
    end
endmodule
