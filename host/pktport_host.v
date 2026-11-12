// T103, host side of the generic packet port (benched in sim/pktport): a crossing FIFO (cpu_clk -> clk50, 2048 x 9 = one RAMB18)
// and the stream side top.v's RMII arbiter already knows (st_req, st_data/valid/eof, pop = own_st & x_ready).
//   grant: only when a whole frame (RESERVE bytes) fits, so the core never stalls inside a frame;
//   st_req: only when a whole frame is in the FIFO, so the MAC side never runs dry inside a frame
//   (rmii_transceiver: data held from valid until ready, no gaps).
//   dc (core swap, cpu_clk): the core's port is forced idle by top.v; a frame it left half written is closed with an eof byte
//   so the MAC side never sees a frame without an end, and no new grant is given.
// NEG_FULLRATE (bench control): ready every clock. NEG_NOREADY (bench control): no reservation, no back-pressure, FIFO of 512 bytes.
module pktport_host #(
`ifdef NEG_NOREADY
    parameter AW = 9,
`else
    parameter AW = 11,
`endif
    parameter RESERVE = 1024
) (
    input  wire       wclk, wrst,                 // cpu_clk
    input  wire       dc,                         // core_decouple in cpu_clk (dfx_sync)
    input  wire       pp_req,
    output reg        pp_grant,
    input  wire [7:0] pp_data,
    input  wire       pp_valid, pp_eof,
    output wire       pp_ready,
    input  wire       rclk, rrst,                 // clk50
    output wire       st_req,
    output wire [7:0] st_data,
    output wire       st_valid, st_eof,
    input  wire       pop                         // own_st & x_ready
);
    localparam DEPTH = 1 << AW;
    (* ram_style = "block" *) reg [8:0] mem [0:DEPTH-1];
    function [AW:0] b2g(input [AW:0] b); b2g = b ^ (b >> 1); endfunction
    function [AW:0] g2b(input [AW:0] g); integer i; begin g2b[AW] = g[AW];
        for (i = AW-1; i >= 0; i = i-1) g2b[i] = g2b[i+1] ^ g[i]; end endfunction

    // ---- write side (wclk)
    reg  [AW:0] wptr = 0, wgray = 0;
    reg  [AW:0] rptr = 0, rgray = 0;
    (* ASYNC_REG = "TRUE" *) reg [AW:0] rg_s1 = 0, rg_s2 = 0;   // read pointer, gray, synchronised
    reg         eof_tgl = 0;
    wire [AW:0] used = wptr - g2b(rg_s2);
    wire        full = used == DEPTH;
    reg  inframe = 0;
    wire close = dc & inframe & ~full;            // decoupled mid-frame: one eof byte closes it
`ifdef NEG_NOREADY
    wire push = pp_valid | close;
    assign pp_ready = 1'b1;
`else
    wire push = close | (pp_valid & pp_ready & ~dc);
`ifdef NEG_FULLRATE
    assign pp_ready = pp_grant & ~full;           // ready every clock: the senders' RAM read lags by one
`else
    reg ph = 0;
    always @(posedge wclk) ph <= ~ph;
    assign pp_ready = pp_grant & ~full & ph;      // at most every second clock: udp_stream/udp_audio read their RAM
`endif                                            // through a register and need one clock between two bytes
`endif
    always @(posedge wclk) begin
        rg_s1 <= rgray; rg_s2 <= rg_s1;
        if (wrst) begin pp_grant <= 0; wptr <= 0; wgray <= 0; eof_tgl <= 0; inframe <= 0; end
        else begin
            if (push) begin
                mem[wptr[AW-1:0]] <= close ? 9'h100 : {pp_eof, pp_data};
                inframe <= close ? 1'b0 : !pp_eof;
                wptr <= wptr + 1'b1; wgray <= b2g(wptr + 1'b1);
                if (close | pp_eof) eof_tgl <= ~eof_tgl;
            end
`ifdef NEG_NOREADY
            pp_grant <= pp_req;
`else
            if (dc || (push && pp_eof)) pp_grant <= 1'b0;
            else if (pp_req && !pp_grant && (DEPTH - used) >= RESERVE) pp_grant <= 1'b1;
`endif
        end
    end

    // ---- read side (rclk): output register in front of the RAM, a frame counter from the eof toggle
    (* ASYNC_REG = "TRUE" *) reg [AW:0] wg_s1 = 0, wg_s2 = 0;
    (* ASYNC_REG = "TRUE" *) reg [2:0]  et_s = 0;
    reg [2:0]  pending = 0;
    reg        ovalid = 0;
    reg [8:0]  odata;
    wire       empty = rptr == g2b(wg_s2);
    wire       fetch = !empty && (!ovalid || pop);
    wire       got   = et_s[2] != et_s[1];
    wire       done  = pop && ovalid && odata[8];
    always @(posedge rclk) begin
        wg_s1 <= wgray; wg_s2 <= wg_s1;
        et_s  <= {et_s[1:0], eof_tgl};
        if (rrst) begin rptr <= 0; rgray <= 0; ovalid <= 0; pending <= 0; end
        else begin
            if (fetch) begin odata <= mem[rptr[AW-1:0]]; rptr <= rptr + 1'b1; rgray <= b2g(rptr + 1'b1); ovalid <= 1'b1; end
            else if (pop) ovalid <= 1'b0;
            pending <= pending + got - done;
        end
    end
    assign st_req   = pending != 0;
    assign st_data  = odata[7:0];
    assign st_eof   = odata[8];
    assign st_valid = ovalid;
endmodule
