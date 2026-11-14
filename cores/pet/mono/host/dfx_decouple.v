// T79.1 decoupler helpers (docs/specs/done/T79.1-host-core-contract.md section 7). The host side of the
// partition edge; layer 2 needs nothing from them. While `dc` is 0 every module is transparent.

// two-flip-flop synchroniser of a level into clk
module dfx_sync (
    input  wire clk,
    input  wire i,
    output wire o
);
    (* ASYNC_REG = "TRUE" *) reg [1:0] s = 2'b00;
    always @(posedge clk) s <= {s[0], i};
    assign o = s[1];
endmodule

// A toggle from the host into the core (clk = the toggle's clock). While dc = 1 the core sees 0; `off`
// follows the host's toggle meanwhile, so at release the core still sees 0 and the next host toggle
// is the first edge it gets: the core (reset, edge detectors cleared) agrees with the host.
module dfx_h2c_tgl (
    input  wire clk,
    input  wire dc,
    input  wire i,
    output wire o
);
    reg off = 1'b0;
    always @(posedge clk) if (dc) off <= i;
    assign o = dc ? 1'b0 : (i ^ off);
endmodule

// A toggle from the core to the host (clk = the host consumer's clock; i is asynchronous to it).
// While dc = 1 the host sees a constant, flipped once at the start (a completion edge for a host
// that waits on it); `off` takes that value, so the new core's toggle (0 after reset) continues
// without an edge at release.
module dfx_c2h_tgl (
    input  wire clk,
    input  wire dc,
    input  wire i,
    output wire o
);
    wire s;
    dfx_sync sy (.clk(clk), .i(i), .o(s));
    reg off = 1'b0, hold = 1'b0, dcq = 1'b0;
    wire run = s ^ off;
    always @(posedge clk) begin
        dcq <= dc;
        if (!dc) hold <= run;
        else begin
            if (!dcq) hold <= ~hold;
            off <= hold;
        end
    end
    assign o = dc ? hold : run;
endmodule

// The drive's DDR2 client (mig_arb client 2, toggle handshake, clk = cpu_clk). While dc = 1 the arbiter
// sees no new request (the toggle is frozen) and the core sees done = 0. At release both offsets make the
// pair agree: the arbiter's request toggle stays where it is while the new core starts at 0, and the
// arbiter's done toggle is offset to 0 for the core. The payload is not held: the arbiter must be idle
// when the partial loads (set dc, wait about 1 ms, then load).
module dfx_drv_port (
    input  wire        clk,
    input  wire        dc,
    input  wire        c_req_tgl,
    output wire        c_done_tgl,
    output wire        a_req_tgl,
    input  wire        a_done_tgl
);
    reg offa = 1'b0, offb = 1'b0, a_frz = 1'b0;
    wire done_s;
    dfx_sync sy (.clk(clk), .i(a_done_tgl), .o(done_s));
    always @(posedge clk) begin
        if (!dc) a_frz <= a_req_tgl;
        else begin
            offa <= a_frz;
            offb <= done_s;
        end
    end
    assign a_req_tgl  = dc ? a_frz : (c_req_tgl ^ offa);
    assign c_done_tgl = dc ? 1'b0 : (a_done_tgl ^ offb);
endmodule
