// DDR2 access for the board glue: the controller (ddr2_ctrl / ddr2_lean; MIG's app_* interface, hence
// the names) behind one arbiter (mig_arb).
//
// ddr2_port: one 32-bit access at a time from a clock of its own, for simple
// clients (JTAG, REU): no cache; a read takes about 0.4 us (measured with the
// MIG; mostly the controller's own latency, ddr2_lean's is about 110 ns + queue), a write is done once mig_arb
// has handed it to the controller. Hold addr/we/wdata/be stable with req high until ack; ack comes for
// one clk with rdata (reads). addr is a 16-bit word address (26 bits = 128 MB);
// an access covers the 32 bits at addr & ~1, be selects their bytes
// (be[0] = byte at 2 * (addr & ~1)). A 16-bit client puts its word into the
// half addr[0] picks.
// Towards mig_arb it is a toggle per request (req_tgl) and one back (done_tgl);
// addr/we/wdata/be go across as they are, stable while the request is open.
module ddr2_port (
    input  wire         clk,
    input  wire         req,
    input  wire [25:0]  addr,
    input  wire         we,
    input  wire [31:0]  wdata,
    input  wire [3:0]   be,
    output reg          ack,
    output reg  [31:0]  rdata,

    output reg          req_tgl = 1'b0,
    input  wire         done_tgl,           // mig_arb (ui_clk)
    input  wire [31:0]  done_rdata          // mig_arb, stable when done_tgl toggles
);
    reg busy = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] done_s = 2'b00;
    always @(posedge clk) begin
        done_s <= {done_s[0], done_tgl};
        ack <= 1'b0;
        if (req && !busy && !ack) begin
            req_tgl <= ~req_tgl;
            busy    <= 1'b1;
        end else if (busy && done_s[1] == req_tgl) begin
            busy  <= 1'b0;
            ack   <= 1'b1;
            rdata <= done_rdata;
        end
    end
endmodule

// mig_arb: MIG's app port for three clients with a toggle handshake from
// their own clocks and one access open at a time each: 0 = cpu_mem_bridge
// (the RISC-V's bus), 1 = ddr2_port (REU, JTAG), 2 = the drive (drive_mem);
// and two that offer MIG commands of their own in ui_clk: cart_fill (c_*: a
// line is four reads in a row, a byte one write) and the video client
// pix_layer (v_*: a pixel-layer line is up to 256 reads in a row, a still
// word one write). Commands go to MIG back to back, one per ui_clk, without
// waiting for read data (MIG keeps them in order, strict ordering):
// cart_fill first, then the drive, then the video client and the pair
// {RISC-V, REU/JTAG} in turn when both wait (the pair itself in turn), or
// the video client before the pair while it says v_urgent (a line fetch that
// has taken too long). The engine client (trx_engine.v, e_*: one command at a
// time, the frame ring's records and the mover) goes after the drive and an
// urgent video client, before the others; its read data comes on c_rd_data. The
// pair has one access open per client, so the video client gets at least
// every other command while it streams; the drive's and the cartridge's
// latency do not change at the arbiter; they do wait behind the video reads
// already in MIG's queue, so the video client gets v_rdy only while fewer
// than V_READS (8) reads are on their way (sim/lat: drive read max 670 ->
// 730 ns, cart line max 546 -> 706 ns with the worst-case video load; the
// read queue holds 32: 8 + 4 cart_fill + 3).
// A write is done once MIG took command and data; a read when its data is
// back. 128-bit words, BL8, app_addr counts 16-bit words. A client access is
// the 32-bit word at its 16-bit word address & ~1; the drive's reads get the
// whole 128-bit word (rdata_line).
module mig_arb #(
    parameter [4:0] V_READS = 5'd8       // video reads in flight at most
) (
    input  wire         ui_clk,
    input  wire         ui_rst,
    input  wire         calib_done,

    input  wire [2:0]   req_tgl,            // per client, from its clock
    input  wire [77:0]  addr,               // {client 2, 1, 0}: 16-bit word addresses
    input  wire [2:0]   we,
    input  wire [95:0]  wdata,
    input  wire [11:0]  be,
    output reg  [2:0]   done_tgl = 3'b000,
    output reg  [63:0]  rdata,              // {client 1, client 0}
    output reg  [127:0] rdata_line,         // client 2
    output reg  [127:0] rline0,             // T133: the whole 128-bit word of client 0's last read (ui_clk, stable until its next read answers)

    input  wire [26:0]  c_addr,             // cart_fill: a write offers both (the C128 has none with NO_CART_EMU, T257 D11: tied to 0, c_en stays 0)
    input  wire [2:0]   c_cmd,              // command and data at once
    input  wire         c_en,
    output wire         c_rdy,
    input  wire [127:0] c_wdf_data,
    input  wire [15:0]  c_wdf_mask,
    input  wire         c_wdf_wren,
    output wire         c_wdf_rdy,
    output wire [127:0] c_rd_data,
    output wire         c_rd_data_valid,

    input  wire [26:0]  v_addr,             // pix_layer, as cart_fill
    input  wire [2:0]   v_cmd,
    input  wire         v_en,
    output wire         v_rdy,
    input  wire [127:0] v_wdf_data,
    input  wire [15:0]  v_wdf_mask,
    input  wire         v_wdf_wren,
    output wire         v_wdf_rdy,
    output wire         v_rd_data_valid,    // data on c_rd_data
    input  wire         v_urgent,           // before the pair, not only in turn

    input  wire [26:0]  e_addr,             // trx_engine, as cart_fill
    input  wire [2:0]   e_cmd,
    input  wire         e_en,
    output wire         e_rdy,
    input  wire [127:0] e_wdf_data,
    input  wire [15:0]  e_wdf_mask,
    input  wire         e_wdf_wren,
    output wire         e_rd_data_valid,    // data on c_rd_data

    // T133: toggles for every write into the RISC-V's cacheable window (byte address < 0xEA0000) that is not
    // client 0's (client 0 = the RISC-V and the MAC, WiFi and USB DMA behind it, seen exactly in cpu_clk):
    // cart_fill, the drive, REU / JTAG window / sampler, the engine, the video client. The data cache
    // (rv_dcache) clears itself on every edge.
    output reg          ext_wr_tgl = 1'b0,

    output reg  [26:0]  app_addr,
    output reg  [2:0]   app_cmd,
    output reg          app_en = 1'b0,
    input  wire         app_rdy,
    output reg  [127:0] app_wdf_data,
    output reg  [15:0]  app_wdf_mask,
    output reg          app_wdf_wren = 1'b0,
    input  wire         app_wdf_rdy,
    input  wire [127:0] app_rd_data,
    input  wire         app_rd_data_valid
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] req_s0 = 3'b000, req_s1 = 3'b000;
    reg  [2:0] seen = 3'b000;
    wire [2:0] pending = req_s1 ^ seen;
    reg        last = 1'b1;                 // client 0 or 1 served last

    // reads on their way, in order: video, client (3 = cart_fill) and lane
    reg  [4:0] rq [0:31];
    reg  [4:0] rq_wr = 5'd0, rq_rd = 5'd0;
    wire [4:0] rq_n = rq_wr - rq_rd;
    reg        vturn = 1'b0;                // video next, when the pair waits too

    // the next command can go out when MIG took the previous one
    wire       ok   = calib_done && !ui_rst &&
                      (!app_en || app_rdy) && (!app_wdf_wren || app_wdf_rdy);
    wire       cart = ok && c_en;
    wire       pair = pending[1] | pending[0];
    wire       v_ok = ok && !c_en && !pending[2] && v_en && rq_n < V_READS;
    wire       eng  = ok && !c_en && !pending[2] && e_en && !(v_ok && v_urgent);
    wire       vid  = v_ok && !eng && (!pair || vturn || v_urgent);
    wire       go   = ok && !c_en && pending != 3'b000 && !vid && !eng;
    wire [1:0] pick = pending[2] ? 2'd2 :
                      (pending[1] && (!pending[0] || !last)) ? 2'd1 : 2'd0;
    wire [25:0] p_addr  = addr[26*pick +: 26];
    wire        p_we    = we[pick];
    wire [31:0] p_wdata = wdata[32*pick +: 32];
    wire [3:0]  p_be    = be[4*pick +: 4];

    wire [4:0]  r = rq[rq_rd];
    wire        take = cart || eng || vid || go;
    wire [1:0]  src  = cart ? 2'd0 : eng ? 2'd1 : vid ? 2'd2 : 2'd3;
    reg  [26:0] s_addr;
    reg  [2:0]  s_cmd;
    reg  [127:0] s_data;
    reg  [15:0] s_mask;
    reg  [4:0]  s_tag;
    always @(*)
        case (src)
        2'd0: begin s_addr = c_addr; s_cmd = c_cmd; s_data = c_wdf_data; s_mask = c_wdf_mask; s_tag = 5'b01100; end
        2'd1: begin s_addr = e_addr; s_cmd = e_cmd; s_data = e_wdf_data; s_mask = e_wdf_mask; s_tag = 5'b01110; end
        2'd2: begin s_addr = v_addr; s_cmd = v_cmd; s_data = v_wdf_data; s_mask = v_wdf_mask; s_tag = 5'b10000; end
        default: begin
            s_addr = {1'b0, p_addr[25:3], 3'b000};
            s_cmd  = p_we ? 3'b000 : 3'b001;
            s_data = {4{p_wdata}};
            s_mask = ~({12'd0, p_be} << {p_addr[2:1], 2'b00});
            s_tag  = {1'b0, pick, p_addr[2:1]};
        end
        endcase
    assign c_rdy           = cart;
    assign c_wdf_rdy       = cart;
    assign c_rd_data       = app_rd_data;
    assign c_rd_data_valid = app_rd_data_valid && r == 5'b01100;
    assign v_rdy           = vid;
    assign v_wdf_rdy       = vid;
    assign v_rd_data_valid = app_rd_data_valid && r[4];
    assign e_rdy           = eng;
    assign e_rd_data_valid = app_rd_data_valid && r == 5'b01110;

    always @(posedge ui_clk) begin
        req_s0 <= req_tgl;
        req_s1 <= req_s0;
        if (app_en && app_rdy) app_en <= 1'b0;
        if (app_wdf_wren && app_wdf_rdy) app_wdf_wren <= 1'b0;

        // the command taken this clock: one encoded select for the whole
        // datapath (s_*), so each bit is one 4:1 mux
        if (take) begin
            app_addr     <= s_addr;
            app_cmd      <= s_cmd;
            app_en       <= 1'b1;
            app_wdf_data <= s_data;
            app_wdf_mask <= s_mask;
            if (s_cmd == 3'b001) begin
                rq[rq_wr] <= s_tag;
                rq_wr     <= rq_wr + 1'b1;
            end else
                app_wdf_wren <= 1'b1;
        end
        if (take && s_cmd == 3'b000 && !(src == 2'd3 && pick == 2'd0) && s_addr < 27'h0750000)
            ext_wr_tgl <= ~ext_wr_tgl;
        if (vid) vturn <= 1'b0;
        if (go) begin
            if (pick != 2'd2) vturn <= 1'b1;
            seen[pick] <= ~seen[pick];
            if (pick != 2'd2) last <= pick[0];
            // a write's client is free once its write is with MIG: done now,
            // the next command (any client's) goes after it
            if (p_we) done_tgl[pick] <= ~done_tgl[pick];
        end

        if (app_rd_data_valid) begin
            rq_rd <= rq_rd + 1'b1;
            if (!r[4]) begin
                if (r[3:2] == 2'd2) rdata_line <= app_rd_data;
                else if (r[3:2] != 2'd3) rdata[32*r[2] +: 32] <= app_rd_data[32*r[1:0] +: 32];
                if (r[3:2] == 2'd0) rline0 <= app_rd_data;
                if (r[3:2] != 2'd3) done_tgl[r[3:2]] <= ~done_tgl[r[3:2]];
            end
        end

        if (ui_rst || !calib_done) begin
            app_en <= 1'b0; app_wdf_wren <= 1'b0;
            rq_wr <= 5'd0; rq_rd <= 5'd0;
        end
    end
endmodule

// cpu_mem_bridge: Gideon's 32-bit memory bus (t_mem_req_32 / t_mem_resp_32,
// flattened by ultimate_sys) onto a mig_arb client, from the CPU clock.
// A request is taken (rack with its tag) when the previous access is done;
// writes are posted, reads answer later with dack_tag and data. The CPU sees
// the bottom 64 MB of the DDR2 (byte address = 2 x word address).
// A CPU reset may come while an access is open: it still completes on the
// MIG side, and its read data is dropped.
module cpu_mem_bridge (
    input  wire         clk,
    input  wire         rst,
    input  wire         request,
    input  wire [7:0]   tag,
    input  wire         read_writen,
    input  wire [25:0]  address,            // byte address, bits 1:0 = 0
    input  wire [3:0]   byte_en,
    input  wire [31:0]  wdata,
    output reg  [31:0]  rdata,
    output reg          rack = 1'b0,
    output reg  [7:0]   rack_tag = 8'h00,
    output reg  [7:0]   dack_tag = 8'h00,

    output reg          req_tgl = 1'b0,
    output reg  [25:0]  a_addr,             // 16-bit word address
    output reg          a_we,
    output reg  [31:0]  a_wdata,
    output reg  [3:0]   a_be,
    input  wire         done_tgl,           // mig_arb (ui_clk)
    input  wire [31:0]  done_rdata,
    // T133: the 128-bit word of the last read and its line address (address[25:4]), valid from the clock the read's dack is given until the next read answers;
    // the data caches fill whole 16-byte lines from it
    input  wire [127:0] done_rline,
    output reg  [127:0] line,
    output reg  [21:0]  line_adr
);
    (* ASYNC_REG = "TRUE" *) reg [1:0] done_s = 2'b00;
    wire idle = (done_s[1] == req_tgl);
    reg       rd_wait = 1'b0;
    reg [7:0] rd_tag;
    reg [21:0] rd_ladr;
    always @(posedge clk) begin
        done_s   <= {done_s[0], done_tgl};
        rack     <= 1'b0;
        rack_tag <= 8'h00;
        dack_tag <= 8'h00;
        if (rd_wait && idle) begin
            rdata    <= done_rdata;
            line     <= done_rline;
            line_adr <= rd_ladr;
            dack_tag <= rd_tag;
            rd_wait  <= 1'b0;
        end else if (request && idle && !rd_wait && !rack && !rst) begin
            a_addr   <= {1'b0, address[25:1]};
            a_we     <= !read_writen;
            a_wdata  <= wdata;
            a_be     <= byte_en;
            req_tgl  <= ~req_tgl;
            rack     <= 1'b1;
            rack_tag <= tag;
            rd_wait  <= read_writen;
            rd_tag   <= tag;
            rd_ladr  <= address[25:4];
        end
        if (rst) rd_wait <= 1'b0;
    end
endmodule
