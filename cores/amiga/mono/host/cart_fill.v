// The DDR2 side of cart_slot (ui_clk). Takes cart_slot's requests one at a
// time (a toggle with the arguments held stable, answered by a toggle):
//   fetch a line: 64 bytes of the ROM image (DDR2 byte base + addr, the base picked by
//                 rom_sel, below) or
//                 of the cartridge RAM (0xEF0000 + addr, addr b22 set; the image offset is
//                 {addr[23], addr[21:0]}, 8 MiB for EasyFlash 3, T51), four
//                 MIG reads, into the cache's block RAM at the given slot
//   write a byte: into the cached line (when cached) and into DDR2; with rbw
//                 (the write log is on, trx_engine.v) a cartridge RAM byte is
//                 read from DDR2 first and handed to the log as ob_* (one
//                 pulse per write, in order)
// and, between them, a look for the lens unit (dma_win.v, T12 §11
// Q2, T21 §3.4.1): one byte of the ROM image or the cartridge RAM (pk_addr as req_addr),
// one MIG read, answered by a toggle with the byte; nothing is written to the
// cache. cart_slot's requests go first, so a write it queued before the
// machine stopped is in DDR2 before a look reads the byte.
// The MIG commands go to mig_arb, which puts them before everyone else's.
// The ROM image's base (T60), by rom_sel (trx_io ROM_SEL; MIG addresses, 16-bit words):
//   0  ROM_BASE_LEGACY  byte 0x3C00000: Gideon's layout, the reset value (his firmware writes
//                       the CRT there, Run PRG too)
//   1  ROM_BASE_BLOCK   byte 0x1000000: the expansion-port block, the REU's 16 MB (the
//                       app's CRT image; the app never has REU and a cartridge on together)
//   2  ROM_BASE_SLOT    byte 0xF00000: the built-in cartridges' slot (Run PRG's boot cart,
//                       SID / MOD players), outside the block so that they never overwrite REU data
module cart_fill #(
    parameter [26:0] ROM_BASE_LEGACY = 27'h1E00000,
    parameter [26:0] ROM_BASE_BLOCK  = 27'h0800000,
    parameter [26:0] ROM_BASE_SLOT   = 27'h0780000,
    parameter [26:0] RAM_BASE = 27'h0778000
) (
    input  wire         ui_clk,
    input  wire         ui_rst,
    input  wire   [1:0] rom_sel,            // another clock, static while a cartridge runs

    input  wire         req_tgl,
    input  wire         req_we,
    input  wire  [23:0] req_addr,
    input  wire   [7:0] req_data,
    input  wire   [5:0] req_slot,
    input  wire         req_cached,
    output reg          ack_tgl = 1'b0,

    output reg    [7:0] c_be = 8'd0,
    output reg    [8:0] c_addr,
    output reg   [63:0] c_data,

    output reg   [26:0] app_addr,
    output reg    [2:0] app_cmd,
    output reg          app_en = 1'b0,
    input  wire         app_rdy,
    output reg  [127:0] app_wdf_data,
    output reg   [15:0] app_wdf_mask,
    output reg          app_wdf_wren = 1'b0,
    input  wire         app_wdf_rdy,
    input  wire [127:0] app_rd_data,
    input  wire         app_rd_data_valid,

    input  wire         rbw,                // another clock: read before writing RAM
    output reg          ob_v = 1'b0,
    output reg   [14:0] ob_addr,
    output reg    [7:0] ob_old,

    input  wire         pk_tgl,             // another clock, pk_addr held from the toggle to the ack
    input  wire  [23:0] pk_addr,
    output reg          pk_ack_tgl = 1'b0,
    output reg    [7:0] pk_data = 8'h00
);
    (* ASYNC_REG = "TRUE" *) reg [1:0] rbw_s = 2'b00;
    always @(posedge ui_clk) rbw_s <= {rbw_s[0], rbw};
    (* ASYNC_REG = "TRUE" *) reg [2:0] req_s = 3'b000;
    reg  seen = 1'b0;
    wire pend = req_s[2] != seen;
    (* ASYNC_REG = "TRUE" *) reg [2:0] pk_s = 3'b000;
    reg  pk_seen = 1'b0;
    wire pk_pend = pk_s[2] != pk_seen;
    (* ASYNC_REG = "TRUE" *) reg [1:0] sel_s1 = 2'd0, sel_s2 = 2'd0;
    always @(posedge ui_clk) begin sel_s1 <= rom_sel; sel_s2 <= sel_s1; end
    wire [26:0] rom_base = (sel_s2 == 2'd1) ? ROM_BASE_BLOCK :
                           (sel_s2 == 2'd2) ? ROM_BASE_SLOT  : ROM_BASE_LEGACY;

    wire [26:0] pk_waddr = pk_addr[22] ? RAM_BASE + {12'd0, pk_addr[15:1]}
                                       : rom_base + {5'd0, pk_addr[23], pk_addr[21:1]};

    // the request's DDR2 word address
    wire [26:0] waddr = req_addr[22] ? RAM_BASE + {12'd0, req_addr[15:1]}
                                     : rom_base + {5'd0, req_addr[23], req_addr[21:1]};

    localparam S_IDLE = 3'd0, S_FETCH = 3'd1, S_WRITE = 3'd2, S_RBW = 3'd3, S_PEEK = 3'd4;
    reg  [2:0]   st = S_IDLE;
    reg          rbw_done = 1'b0;
    reg  [2:0]   issued, got;
    reg  [3:0]   put;
    reg  [127:0] lb [0:3];
    reg          cmd_ok, data_ok;

    always @(posedge ui_clk) begin
        req_s <= {req_s[1:0], req_tgl};
        pk_s  <= {pk_s[1:0], pk_tgl};
        c_be  <= 8'd0;
        ob_v  <= 1'b0;
        if (ui_rst) begin
            st <= S_IDLE; app_en <= 1'b0; app_wdf_wren <= 1'b0;
            seen <= req_s[2];
            pk_seen <= pk_s[2];
        end else case (st)
            S_IDLE:
                if (pend) begin
                    app_addr <= {waddr[26:3], 3'b000};
                    if (req_we && req_addr[22] && rbw_s[1] && !rbw_done) begin
                        app_cmd <= 3'b001;
                        app_en  <= 1'b1;
                        st <= S_RBW;
                    end else if (req_we) begin
                        rbw_done <= 1'b0;
                        if (req_cached) begin
                            c_be   <= 8'd1 << req_addr[2:0];
                            c_addr <= {req_slot, req_addr[5:3]};
                            c_data <= {8{req_data}};
                        end
                        app_cmd      <= 3'b000;
                        app_wdf_data <= {16{req_data}};
                        app_wdf_mask <= ~(16'd1 << req_addr[3:0]);
                        app_en       <= 1'b1;
                        app_wdf_wren <= 1'b1;
                        cmd_ok <= 1'b0; data_ok <= 1'b0;
                        st <= S_WRITE;
                    end else begin
                        app_cmd <= 3'b001;
                        app_en  <= 1'b1;
                        issued <= 3'd0; got <= 3'd0; put <= 4'd0;
                        st <= S_FETCH;
                    end
                end else if (pk_pend) begin
                    app_addr <= {pk_waddr[26:3], 3'b000};
                    app_cmd  <= 3'b001;
                    app_en   <= 1'b1;
                    st <= S_PEEK;
                end
            S_FETCH: begin
                if (app_en && app_rdy) begin
                    issued   <= issued + 1'b1;
                    app_addr <= app_addr + 27'd8;
                    if (issued == 3'd3) app_en <= 1'b0;
                end
                if (app_rd_data_valid) begin
                    lb[got[1:0]] <= app_rd_data;
                    got <= got + 1'b1;
                end
                // into the cache as the 16-byte words arrive, 8 bytes a clock
                if (put != 4'd8 && {1'b0, put[3:1]} < got) begin
                    c_be   <= 8'hFF;
                    c_addr <= {req_slot, put[2:0]};
                    c_data <= put[0] ? lb[put[2:1]][127:64] : lb[put[2:1]][63:0];
                    put    <= put + 1'b1;
                end
                if (put == 4'd8) begin
                    seen <= ~seen;
                    ack_tgl <= ~ack_tgl;
                    st <= S_IDLE;
                end
            end
            S_WRITE: begin
                if (app_en && app_rdy) begin app_en <= 1'b0; cmd_ok <= 1'b1; end
                if (app_wdf_wren && app_wdf_rdy) begin app_wdf_wren <= 1'b0; data_ok <= 1'b1; end
                if ((cmd_ok || (app_en && app_rdy)) && (data_ok || (app_wdf_wren && app_wdf_rdy))) begin
                    seen <= ~seen;
                    ack_tgl <= ~ack_tgl;
                    st <= S_IDLE;
                end
            end
            S_RBW: begin                    // the old byte, then the write as above
                if (app_en && app_rdy) app_en <= 1'b0;
                if (app_rd_data_valid) begin
                    ob_v     <= 1'b1;
                    ob_addr  <= req_addr[14:0];
                    ob_old   <= app_rd_data[8 * req_addr[3:0] +: 8];
                    rbw_done <= 1'b1;
                    st <= S_IDLE;
                end
            end
            S_PEEK: begin                   // the lens unit's look: one byte
                if (app_en && app_rdy) app_en <= 1'b0;
                if (app_rd_data_valid) begin
                    pk_data    <= app_rd_data[8 * pk_addr[3:0] +: 8];
                    pk_seen    <= ~pk_seen;
                    pk_ack_tgl <= ~pk_ack_tgl;
                    st <= S_IDLE;
                end
            end
            default: st <= S_IDLE;
        endcase
    end
endmodule
