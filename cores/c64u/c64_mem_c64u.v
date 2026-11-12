// (c64u copy of trx64/c64_mem.v without the write-log taps: T103 Q5)
// Block RAM in place of icepi's SDRAM port p0 (tiny_sdram word interface).
//
// c64_top_c64u puts out word addresses {bank, 0, byte_addr[21:1]}; byte map:
//   0x00000-0x0FFFF  C64 RAM            -> ram (read/write)
//   0x10000-0x17FFF  char/kernal/basic  -> rom (port A reads only)
//   0x18000-          cartridge RAM/ROM  -> reads 0xFF (icepi's own cartridge
//                                          is not used: cart_slot_c64u serves them)
// Every command is taken at once (cmd_ready = 1); read data comes one clock
// later with rdata_valid and stays until the next read. The second port runs
// on the falling edge of clk2x (T29), which keeps going while the machine
// pause stops clk. Two users share it, one access per falling edge: the
// outside (dbg_*: JTAG, the app's DMA window, the text
// layer; a request is dbg_req, staged on dbg_clk's edge, takes the first falling
// edge after it, and its data is held in dbg_rdata) and the turbo CPU
// (c64_turbo_c64u.v, tb_*, every falling edge the outside leaves it; tb_took
// says the outside had the last one and the CPU must read again). With
// dbg_rom the outside reads and writes the ROM block instead (byte offset
// dbg_addr[14:0]: char 0x0000, KERNAL 0x2000, BASIC 0x4000), for the app's DMA
// window and for the Ultimate app, which writes the ROMs it loads from
// /roms through its apertures (ultimate_sys.vhd, dma_win_c64u.v).
//
// The ROM block starts blank: no Commodore ROM is in the bitstream. Only a
// simulation built with TRX_SIM_ROMS preloads it from mem/c64_rom.mem
// (tools/rom2mem.py, the sim's run directory).
module c64_mem_c64u (
    input  wire        clk,
    input  wire        dbg_clk,
    input  wire        clk2x,
    input  wire        dbg_req,
    // the turbo CPU's access (c64_turbo_c64u.v): RAM at tb_addr, ROM at the ROM
    // block's byte address tb_raddr (read), a RAM write when tb_we
    input  wire [15:0] tb_addr,
    input  wire [14:0] tb_raddr,
    input  wire        tb_we,
    input  wire  [7:0] tb_wdata,
    // the four bytes of the addressed word as the falling edge read them (the
    // CPU picks the lane with its own address bit 0: nothing but wires after the block RAM)
    output wire  [7:0] tb_ram_lo, tb_ram_hi, tb_rom_lo, tb_rom_hi,
    output wire        tb_took,
    input  wire [23:0] cmd_addr,
    input  wire        cmd_we,
    input  wire        cmd_valid,
    output wire        cmd_ready,
    input  wire [15:0] wdata,
    input  wire  [1:0] wdata_we,
    output wire        wdata_ready,
    output reg  [15:0] rdata,
    output reg         rdata_valid,
    // second port into the C64 RAM, one byte per access (debug over JTAG)
    input  wire [15:0] dbg_addr,
    input  wire        dbg_rom,
    input  wire        dbg_we,
    input  wire  [7:0] dbg_wdata,
    output wire  [7:0] dbg_rdata
);
    assign cmd_ready   = 1'b1;
    assign wdata_ready = 1'b1;

    wire [20:0] waddr  = cmd_addr[20:0];
    wire        is_ram = waddr[20:15] == 6'b0;
    wire        is_rom = waddr[20:14] == 7'b0000010;

    (* ram_style = "block" *) reg [7:0] ram_lo [0:32767];
    (* ram_style = "block" *) reg [7:0] ram_hi [0:32767];
    (* ram_style = "block" *) reg [7:0] rom_lo [0:16383];
    (* ram_style = "block" *) reg [7:0] rom_hi [0:16383];
`ifdef TRX_SIM_ROMS
    reg [15:0] rom_init [0:16383];
    integer ri;
    initial begin
        $readmemh("mem/c64_rom.mem", rom_init);
        for (ri = 0; ri < 16384; ri = ri + 1) begin
            rom_lo[ri] = rom_init[ri][7:0];
            rom_hi[ri] = rom_init[ri][15:8];
        end
    end
`endif

    reg  [7:0] ram_lo_q, ram_hi_q;
    reg  [7:0] rom_lo_q, rom_hi_q;
    reg  [1:0] sel;                 // 0 = ram, 1 = rom, 2 = nothing

    // ---- the second port (see the header) ------------------------------------
    // The outside's access (dbg_*) is staged on dbg_clk's edge, where the old dbg_clk block RAM
    // took its address, and taken on the first falling edge after it; its byte is in dbg_rdata
    // half a clock later and stays until the next access's, which is a clock later at the
    // earliest: whoever samples one clock after the edge (the freeze responder, the sims' peek)
    // sees what the old output register showed. One access per clock of a request (a request
    // held over several clocks is served in each of them, so the data is fresh; a one-clock
    // pulse is served once): s_flag toggles on the falling edge that serves, p_flag takes it at the
    // next dbg_clk edge, "s_flag == p_flag" is "not served in this clock yet".
    reg [15:0] e_addr  = 16'd0;
    reg  [7:0] e_wdata = 8'd0;
    reg        e_rom = 1'b0, e_we = 1'b0, e_req = 1'b0;
    reg        s_flag = 1'b0, p_flag = 1'b0;
    reg        e_wr_lo = 1'b0, e_wr_hi = 1'b0;   // e_we && !e_rom, split by e_addr[0]
    always @(posedge dbg_clk) begin
        e_addr <= dbg_addr; e_rom <= dbg_rom; e_we <= dbg_we; e_wdata <= dbg_wdata;
        e_req  <= dbg_req;
        e_wr_lo <= dbg_we && !dbg_rom && !dbg_addr[0];
        e_wr_hi <= dbg_we && !dbg_rom &&  dbg_addr[0];
        p_flag <= s_flag;
    end
    // The select is the same signal in three registers (dont_touch keeps them apart): the address
    // pins, the write enables and the data pins of the block RAM each have theirs, placed by
    // their loads (one register with 65 loads was 2.7 ns of route on the half-cycle path to the
    // pins, T29 §12.9)
    (* dont_touch = "true" *) reg e_req_a = 1'b0;
    (* dont_touch = "true" *) reg e_req_w = 1'b0;
    (* dont_touch = "true" *) reg e_req_d = 1'b0;
    always @(posedge dbg_clk) begin e_req_a <= dbg_req; e_req_w <= dbg_req; e_req_d <= dbg_req; end
    wire       use_ext   = e_req   && (s_flag == p_flag);
    wire       use_ext_a = e_req_a && (s_flag == p_flag);
    wire       use_ext_w = e_req_w && (s_flag == p_flag);
    wire       use_ext_d = e_req_d && (s_flag == p_flag);
    wire [15:0] b_addr  = use_ext_a ? e_addr : tb_addr;
    wire [14:0] b_raddr = use_ext_a ? e_addr[14:0] : tb_raddr;
    // the byte lanes' write enables: the outside's lane decode is registered with e_* (the same
    // function of the same inputs), so each WEBWE pin is one LUT behind tb_we (T29 §12.9)
    wire        b_wram_lo = use_ext_w ? e_wr_lo : (tb_we && !tb_addr[0]);
    wire        b_wram_hi = use_ext_w ? e_wr_hi : (tb_we &&  tb_addr[0]);
    wire        b_wrom  = use_ext_w && e_we && e_rom;
    wire  [7:0] b_wdata = use_ext_d ? e_wdata : tb_wdata;
    reg   [7:0] b_ram_lo, b_ram_hi, b_rom_lo, b_rom_hi;
    reg         b_lane_ram = 1'b0, b_lane_rom = 1'b0, b_ext_rom = 1'b0, took = 1'b0;
    always @(negedge clk2x) begin
        if (b_wram_lo) ram_lo[b_addr[15:1]] <= b_wdata;
        if (b_wram_hi) ram_hi[b_addr[15:1]] <= b_wdata;
        // ROM writes use the read address: b_wrom implies use_ext_a, so b_raddr is e_addr[14:0]
        // then. With a third address (e_addr) Vivado kept a second copy of both ROMs (8 RAMB36).
        if (b_wrom && !b_raddr[0]) rom_lo[b_raddr[14:1]] <= e_wdata;
        if (b_wrom &&  b_raddr[0]) rom_hi[b_raddr[14:1]] <= e_wdata;
        b_ram_lo <= ram_lo[b_addr[15:1]];
        b_ram_hi <= ram_hi[b_addr[15:1]];
        b_rom_lo <= rom_lo[b_raddr[14:1]];
        b_rom_hi <= rom_hi[b_raddr[14:1]];
        b_lane_ram <= b_addr[0];
        b_lane_rom <= b_raddr[0];
        b_ext_rom  <= use_ext && e_rom;
        if (use_ext) s_flag <= ~s_flag;
        took       <= use_ext;
    end
    assign tb_ram_lo = b_ram_lo;
    assign tb_ram_hi = b_ram_hi;
    assign tb_rom_lo = b_rom_lo;
    assign tb_rom_hi = b_rom_hi;
    assign tb_took   = took;
    wire [7:0] tb_rdata_ram = b_lane_ram ? b_ram_hi : b_ram_lo;
    wire [7:0] tb_rdata_rom = b_lane_rom ? b_rom_hi : b_rom_lo;
    // the outside's data: the byte the served access read (a write's old byte: read-first), copied on the first clk2x edge after the falling edge
    reg  [7:0] e_hold = 8'd0;
    always @(posedge clk2x)
        if (took) e_hold <= b_ext_rom ? tb_rdata_rom : tb_rdata_ram;
    assign dbg_rdata = e_hold;

    always @(posedge clk) begin
        if (cmd_valid && is_ram) begin
            if (cmd_we && wdata_we[0]) ram_lo[waddr[14:0]] <= wdata[7:0];
            if (cmd_we && wdata_we[1]) ram_hi[waddr[14:0]] <= wdata[15:8];
        end
        ram_lo_q <= ram_lo[waddr[14:0]];
        ram_hi_q <= ram_hi[waddr[14:0]];
        rom_lo_q <= rom_lo[waddr[13:0]];
        rom_hi_q <= rom_hi[waddr[13:0]];
    end

    always @(posedge clk) begin
        rdata_valid <= cmd_valid && !cmd_we;
        if (cmd_valid && !cmd_we)
            sel <= is_ram ? 2'd0 : is_rom ? 2'd1 : 2'd2;
    end

    // keep the last read word while no new one arrives
    reg [15:0] hold;
    always @(posedge clk) if (rdata_valid) hold <= rdata;

    always @(*) begin
        if (!rdata_valid)       rdata = hold;
        else case (sel)
            2'd0:    rdata = {ram_hi_q, ram_lo_q};
            2'd1:    rdata = {rom_hi_q, rom_lo_q};
            default: rdata = 16'hFFFF;
        endcase
    end
endmodule
