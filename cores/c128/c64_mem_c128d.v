// T140 D25: the C128's memory with the 128 KiB RAM in the DDR2 (c128_ramd.v) and the ROM image behind its line cache (c128_romc.v). Same command interface
// as c64_mem_c128.v (word address {bank, 0, 15 bit}, one command per half cycle, the answer in the clock after), chosen by `RAM_DDR2` (with ROM_CACHE).
// c128_ramd runs on the ungated clock (dbg_clk): the second port (the application's window) and the write FIFO must work while the machine is stopped.
// The command is taken at its first clock (a command held over a closed gate is one event). The turbo port tb_* is not served (NO_TURBO).
`timescale 1ns / 1ps
module c64_mem_c128d (
    input  wire        clk,                  // the machine's gated clock
    input  wire        dbg_clk,              // the ungated clock
    input  wire        clk2x,
    input  wire        dbg_req,
    input  wire [15:0] tb_addr,
    input  wire [14:0] tb_raddr,
    input  wire        tb_we,
    input  wire  [7:0] tb_wdata,
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
    output wire        rdata_valid,
    input  wire [15:0] dbg_addr,
    input  wire        dbg_rom,
    input  wire        dbg_we,
    input  wire  [7:0] dbg_wdata,
    output wire  [7:0] dbg_rdata,
    output wire        dbg_ack,
    input  wire        cmd_vic,              // the RAM command is the VIC's
    input  wire  [2:0] vic_tag,
    output wire        ram_hold,
    // the ROM line cache
    input  wire        rom_flush,
    output wire        rom_req_tgl,
    output wire [12:0] rom_req_line,
    input  wire        rom_done_tgl,
    input  wire [127:0] rom_fill,
    output wire [31:0] rom_n_hit, rom_n_miss, rom_n_wait,
    output wire [15:0] rom_fill_max,
    output wire        rom_busy,
    output wire        rom_miss,             // a ROM or RAM read of the previous clock could not be answered: the CPU repeats it
    output wire        rom_fill_busy,
    // the RAM in the DDR2 (c128_ddrx.v)
    output wire        ram_m_req_tgl,
    output wire [12:0] ram_m_req_line,
    input  wire        ram_m_done_tgl,
    output wire        ram_w_req_tgl,
    output wire [16:0] ram_w_req_addr,
    output wire  [7:0] ram_w_req_data,
    input  wire        ram_w_done_tgl,
    output wire [15:0] ram_n_vic_miss, ram_n_cpu_wait, ram_n_fetch, ram_n_wlost, ram_n_vic_blk1
);
    assign cmd_ready   = 1'b1;
    assign wdata_ready = 1'b1;
    assign tb_ram_lo = 8'd0; assign tb_ram_hi = 8'd0; assign tb_rom_lo = 8'd0; assign tb_rom_hi = 8'd0; assign tb_took = 1'b0;

    wire [20:0] waddr  = cmd_addr[20:0];
    wire        is_ram = waddr[20:16] == 5'b0;
    wire        is_rom = waddr[20:16] == 5'b00001;

    // ---- RAM ----------------------------------------------------------------------------------------------------------------------------
    reg  vq = 1'b0;
    always @(posedge dbg_clk) vq <= cmd_valid && is_ram;
    wire ram_ev = cmd_valid && is_ram && !vq;
    wire        ramd_v, ramd_miss;
    wire [15:0] ramd_word;
    wire [127:0] fill_ram;
    c128_ramd #(.NSLOT(5), .PIN(1), .NDYN(1)) ramd (
        .clk(dbg_clk), .cmd_valid(ram_ev), .cmd_we(cmd_we), .cmd_wa(waddr[15:0]), .cmd_wdata(wdata), .cmd_wlane(wdata_we), .cmd_vic(cmd_vic),
        .vic_tag(vic_tag), .rd_v(ramd_v), .rd_word(ramd_word), .rd_miss(ramd_miss), .hold(ram_hold),
        .pj_req(dbg_req), .pj_addr(dbg_addr), .pj_we(dbg_we), .pj_wdata(dbg_wdata), .pj_rom(dbg_rom), .pj_ack(dbg_ack), .pj_rdata(dbg_rdata),
        .m_req_tgl(ram_m_req_tgl), .m_req_line(ram_m_req_line), .m_done_tgl(ram_m_done_tgl), .fill(rom_fill),
        .w_req_tgl(ram_w_req_tgl), .w_req_addr(ram_w_req_addr), .w_req_data(ram_w_req_data), .w_done_tgl(ram_w_done_tgl),
        .n_vic_miss(ram_n_vic_miss), .n_cpu_wait(ram_n_cpu_wait), .n_fetch(ram_n_fetch), .n_wlost(ram_n_wlost), .n_vic_blk1(ram_n_vic_blk1),
        .slot_tags(), .slot_act(), .slot_full());

    // ---- ROM ----------------------------------------------------------------------------------------------------------------------------
    wire        romc_valid, romc_miss;
    wire [15:0] romc_data;
    wire        romc_ready;
    c128_romc romc (.clk(clk), .flush(rom_flush), .cmd_valid(cmd_valid && is_rom && !cmd_we), .cmd_ready(romc_ready), .cmd_word(waddr[15:0]),
        .rvalid(romc_valid), .rmiss(romc_miss), .rdata(romc_data), .req_tgl(rom_req_tgl), .req_line(rom_req_line), .done_tgl(rom_done_tgl), .fill(rom_fill),
        .n_hit(rom_n_hit), .n_miss(rom_n_miss), .n_wait(rom_n_wait), .fill_max(rom_fill_max), .fill_busy(rom_fill_busy), .busy(rom_busy));
    assign rom_miss = romc_miss || ramd_miss;

    reg rv_other = 1'b0;
    always @(posedge clk) rv_other <= cmd_valid && !cmd_we && !is_rom && !is_ram;
    assign rdata_valid = rv_other || romc_valid || romc_miss || ramd_v || ramd_miss;
    reg [15:0] hold;
    always @(posedge clk) if (rdata_valid) hold <= rdata;
    always @(*) begin
        if (romc_valid)         rdata = romc_data;
        else if (ramd_v)        rdata = ramd_word;
        else if (!rv_other)     rdata = hold;           // a miss: the last word again (the CPU does not take it)
        else                    rdata = 16'hFFFF;
    end
endmodule
