// Copy of cores/c64u/cart_fill_c64u.v, taken 2026-11-23 (T257 D11): the C128 reads its own copy, never the c64u tree. Content unchanged below this line.
// The DDR2 side of cart_slot_c64u (ui_clk). Takes cart_slot_c64u's requests one at a
// time (a toggle with the arguments held stable, answered by a toggle):
//   fetch a line: 64 bytes of the ROM image (DDR2 byte base + addr, the base picked by
//                 rom_sel, below) or
//                 of the cartridge RAM (0xEF0000 + addr, addr b22 set; the image offset is
//                 {addr[23], addr[21:0]}, 8 MiB for EasyFlash 3, T51), four
//                 MIG reads, into the cache's block RAM at the given slot
//   write a byte: into the cached line (when cached) and into DDR2
// (c64u copy of host/cart_fill.v without the write-log read-before-write and without the lens unit's byte look: T103 Q5)
// The MIG commands go to mig_arb, which puts them before everyone else's.
// The ROM image's base (T60), by rom_sel (c64u_io ROM_SEL; MIG addresses, 16-bit words):
//   0  ROM_BASE_LEGACY  byte 0x3C00000: Gideon's layout, the reset value (his firmware writes
//                       the CRT there, Run PRG too)
//   1  ROM_BASE_BLOCK   byte 0x1000000: the expansion-port block, the REU's 16 MB (the
//                       app's CRT image; the app never has REU and a cartridge on together)
//   2  ROM_BASE_SLOT    byte 0xF00000: the built-in cartridges' slot (Run PRG's boot cart,
//                       SID / MOD players), outside the block so that they never overwrite REU data
module cart_fill_c64u #(
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
    input  wire         app_rd_data_valid
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] req_s = 3'b000;
    reg  seen = 1'b0;
    wire pend = req_s[2] != seen;
    (* ASYNC_REG = "TRUE" *) reg [1:0] sel_s1 = 2'd0, sel_s2 = 2'd0;
    always @(posedge ui_clk) begin sel_s1 <= rom_sel; sel_s2 <= sel_s1; end
    wire [26:0] rom_base = (sel_s2 == 2'd1) ? ROM_BASE_BLOCK :
                           (sel_s2 == 2'd2) ? ROM_BASE_SLOT  : ROM_BASE_LEGACY;

    // the request's DDR2 word address
    wire [26:0] waddr = req_addr[22] ? RAM_BASE + {12'd0, req_addr[15:1]}
                                     : rom_base + {5'd0, req_addr[23], req_addr[21:1]};

    localparam S_IDLE = 3'd0, S_FETCH = 3'd1, S_WRITE = 3'd2;
    reg  [2:0]   st = S_IDLE;
    reg  [2:0]   issued, got;
    reg  [3:0]   put;
    reg  [127:0] lb [0:3];
    reg          cmd_ok, data_ok;

    always @(posedge ui_clk) begin
        req_s <= {req_s[1:0], req_tgl};
        c_be  <= 8'd0;
        if (ui_rst) begin
            st <= S_IDLE; app_en <= 1'b0; app_wdf_wren <= 1'b0;
            seen <= req_s[2];
        end else case (st)
            S_IDLE:
                if (pend) begin
                    app_addr <= {waddr[26:3], 3'b000};
                    if (req_we) begin
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
            default: st <= S_IDLE;
        endcase
    end
endmodule
