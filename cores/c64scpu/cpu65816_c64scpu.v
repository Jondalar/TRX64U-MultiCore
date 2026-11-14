// ---------------------------------------------------------------------------
// T182 step 2 (T2, T3): the 65C816 in place of the 6510 - a drop-in for vendor/icepi/cpu6510.v in c64_top_c64scpu.v (same ports where the top uses them).
// The CPU is cores/c64scpu/cpu (s816_cpu, the MiSTer 65C816); the 6510's port $00/$01 is scpu_pport; this module is the bus adaptor to the PHI2 path:
//   - the CPU's clock enable is scpu_mem's core_ce (on clk2x): in 1 MHz mode one step per PHI2 cycle (ce_raw = g_edge && phi2_l, the edge the 6510 core took);
//     in 20 MHz mode (step 4) the rate accumulator's pend (scpu_mem.v, 1/20,000,000 units against PHI2), in both modes held back until the memory is ready (core_ce = tick && ok);
//   - D_IN is the byte the bus left at the last PHI2 fall (din_r), RDY / IRQ / NMI are sampled at the PHI2 fall like the 6510's wrapper did;
//   - RDY: a read cycle with rdy low is repeated (the s816_cpu stops its state on RDY_IN low); a write cycle is not held (the 6510 and the BA rule of the C64:
//     the CPU may finish writes), so rdy_in = rdy | write;
//   - a cycle with neither VDA nor VPA is an internal cycle of the 65816, not a bus access: it is shown to the C64 as a read of $0000 (RAM, no side effect), where the 6510 would
//     have made a dummy read; it still takes its PHI2 cycle;
//   - step 3: the memory map is scpu_mem.v (spec T4/T5/T10): the 24-bit address decides, with the configuration table, whether the cycle is the CPU's own memory (SRAM, SuperRAM,
//     ROM in the DDR2 behind one 8 KiB cache), a C64-bus cycle (the 16-bit address as before, `fwd`) or a SuperCPU hardware register; the CPU's enable is withheld until the data is there;
//     $00/$01 go to the port (scpu_pport);
//   - NMI acknowledge (the keyboard's RESTORE toggle) = the vector pull of $FFFA;
//   - no turbo (the 6510 wrapper's t_*, dout_early/late, din_r_o stay as ports for c64_turbo_c64u in its NO_TURBO form), no state bus (sb_*, st_a..st_p read 0: the
//     65816's registers are not exposed; T182 §4/§5.4).
// `ifdef SCPU_MUT_SLOW: the negative control of the cycle bench - the CPU takes a step in every second PHI2 cycle.
// `ifdef SCPU_MUT_CE2: the negative control of the enable-spacing assertion (tb_c64scpu.v) - a second enable 2 clk2x cycles after each one.
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module cpu65816_c64scpu #(
  parameter [18:0] FALL_OFF_CYCLES = 19'd350000
) (
  input  wire clk,
  input  wire clk2x,
  input  wire g_edge,
  input  wire run2,
  input  wire skip_pl,
  input  wire t_mode, t_ce, t_sel,
  input  wire [7:0] t_din,
  output wire [7:0] dout_early,
  output wire       dout_late,
  output wire [7:0] din_r_o,
  output wire [2:0] pout3_q,

  input  wire reset,
  input  wire clk_phi,
  input  wire phi2_l,
  input  wire phi2_n,
  input  wire rdy,
  input  wire irq,
  input  wire nmi,
  output wire nmi_ack,

  input  wire [ 7:0] din,
  output wire [ 7:0] dout,
  output wire [15:0] addr,
  output wire        we,

  input  wire [5:0] pin,
  output wire [5:0] pout,

  output wire [15:0] pc,
  output wire        e_flag,      // the 65816's emulation flag (E): $D0B6 bit 7 and the vector reroute (later steps)

  // step 3 (scpu_mem.v): the memory map
  input  wire        exrom_pin, game_pin,
  output wire  [2:0] pla_pport,
  input  wire        rom_valid_i,
  input  wire  [4:0] srom_log2_i,
  input  wire  [1:0] scpu_sw_i,
  input  wire        rom_flush,
  input  wire        aec,          // step 5: the VIC leaves the C64 bus to the CPU (the mirror write is repeated when it does not)
  output wire        rom_valid_o,
  output wire        s_req_tgl,
  output wire [20:0] s_req_line,
  input  wire        s_done_tgl,
  input  wire [127:0] s_fill,
  output wire        sw_req_tgl,
  output wire [24:0] sw_req_addr,
  output wire  [7:0] sw_req_data,
  input  wire        sw_done_tgl,
  output wire [31:0] dev_cyc, mem_n_hit, mem_n_miss,
  output wire        bootmap_o, hwenable_o, dosext_o,
  output wire        fast_o,

  output wire [7:0] st_a, st_x, st_y, st_s, st_p,
  output wire       st_sync,
  output wire       st_intg,
  output wire [7:0] st_pdir, st_pout, st_pread,
  output wire [39:0] st_nx,
  input  wire         sb_ld,
  input  wire [208:0] sb_stg,
  output wire [208:0] sb_cur,

  input  wire         dp_we,
  input  wire         dp_hi,
  input  wire   [7:0] dp_data,
  input  wire         dp_restore
);

// ---- the lines, sampled at the PHI2 fall as the 6510's wrapper did --------------------------------------------------------------------
reg       rdy_r = 1'b0, irq_r = 1'b0, nmi_r = 1'b0;
reg [7:0] din_r = 8'd0;
always @(posedge clk) begin
  if (reset) begin
    rdy_r <= 1'b0;
    irq_r <= 1'b0;
    nmi_r <= 1'b0;
  end else if (phi2_n) begin
    rdy_r <= rdy;
    irq_r <= irq;
    nmi_r <= nmi;
    din_r <= din;
  end
end
wire rdy_latched = clk_phi ? rdy : rdy_r;
// A level sample: the low phase also drops it as soon as the line itself falls. The 65816 at 20 MHz runs a whole handler tail (ack write, PLA, RTI) inside the PHI2 cycle of the
// acknowledging write; the sample of the PHI2 fall still holds the old level then, and the interrupt would be taken again (VIC raster: 8 instead of 5 handler runs in 100,000 cycles).
wire irq_latched = clk_phi ? irq : (irq_r & irq);
wire nmi_latched = clk_phi ? nmi : nmi_r;

// ---- the CPU ----------------------------------------------------------------------------------------------------------------------------
reg rst_q = 1'b1;                       // the reset on clk2x: the 65816's reset is asynchronous
wire rom_valid;
`ifdef SCPU_FLATMAP
always @(posedge clk2x) rst_q <= reset;
`elsif SCPU_MUT_NOVALID
always @(posedge clk2x) rst_q <= reset;      // negative control of S1: ROM_VALID = 0 does not hold the CPU
`else
always @(posedge clk2x) rst_q <= reset || !rom_valid;      // T10 ROM_VALID: with 0 the CPU is held in reset, no bus cycle of its own
`endif

`ifdef SCPU_MUT_SLOW
reg slow_q = 1'b0;
always @(posedge clk2x) if (g_edge && phi2_l) slow_q <= ~slow_q;
wire ce_raw = g_edge && phi2_l && slow_q;
`elsif SCPU_MUT_CE2
// negative control of the enable-spacing assertion in sim/tb_c64scpu.v: a second enable two clk2x cycles after every real one
reg ce_d1 = 1'b0, ce_d2 = 1'b0;
always @(posedge clk2x) begin ce_d1 <= g_edge && phi2_l; ce_d2 <= ce_d1; end
wire ce_raw = (g_edge && phi2_l) || ce_d2;
`else
wire ce_raw = g_edge && phi2_l;
`endif
wire core_ce;

wire [23:0] a_out;
wire [ 7:0] d_out, d_in;
wire        rw_n, vpa, vda, vpb, mlb, rdy_out, e_out;
wire        bus_cyc = vda | vpa;                    // a real bus cycle
wire        wr_cyc  = !rw_n;
wire        mem_free, sync_cyc;
wire        mir_drv;                                 // step 5: a mirrored write of the 65816's RAM is on the C64 bus (spec 5.7)
wire [15:0] mir_a;
wire  [7:0] mir_d;
//                      // 20 MHz and not a PHI2-synchronous cycle: BA does not stop it (scpu64mem.c:150-162)
wire        rdy_in  = rdy_latched | wr_cyc | mem_free;

`ifdef SCPU_MUT_EMUONLY
wire [7:0] cpu_din = (vda && vpa && d_in == 8'hFB) ? 8'hEA : d_in;      // S3 control: XCE (opcode $FB) is fetched as NOP, the 65816 stays in emulation mode
`else
wire [7:0] cpu_din = d_in;
`endif
s816_cpu cpu (
  .clk     (clk2x),
  .rst_n   (!rst_q),
  .ce      (core_ce),
  .rdy_in  (rdy_in),
  .nmi_n   (!nmi_latched),
  .irq_n   (!irq_latched),
  .abort_n (1'b1),
  .d_in    (cpu_din),
  .d_out   (d_out),
  .a_out   (a_out),
  .we      (rw_n),
  .rdy_out (rdy_out),
  .e_out   (e_out),
  .vpa     (vpa),
  .vda     (vda),
  .mlb     (mlb),
  .vpb     (vpb)
);

// the memory map: scpu_mem.v decides per cycle where it goes and withholds the enable until the CPU's own memory has the data
wire       fwd;
wire       rd_mem, rd_int;
wire [7:0] mem_q, int_q;
scpu_mem smem (
  .clk2x(clk2x), .rst(reset), .ce_raw(ce_raw), .l_ev(g_edge && phi2_l), .n_ev(g_edge && phi2_n), .core_ce(core_ce),
  .a_out(a_out), .bus_cyc(bus_cyc), .wr_cyc(wr_cyc), .d_out(d_out), .e_flag(e_out), .vpb(vpb), .vpa(vpa), .pport(pout[2:0]), .exrom_pin(exrom_pin), .game_pin(game_pin),
  .rom_valid_i(rom_valid_i), .srom_log2_i(srom_log2_i), .scpu_sw_i(scpu_sw_i), .flush(rom_flush), .aec(aec), .rom_valid_o(rom_valid), .fast_o(fast_o), .mem_free(mem_free), .sync_cyc(sync_cyc), .mir_drv(mir_drv), .mir_a(mir_a), .mir_d(mir_d),
  .fwd(fwd), .pla_pport(pla_pport), .rd_mem(rd_mem), .rd_int(rd_int), .mem_q(mem_q), .int_q(int_q),
  .s_req_tgl(s_req_tgl), .s_req_line(s_req_line), .s_done_tgl(s_done_tgl), .fill(s_fill),
  .sw_req_tgl(sw_req_tgl), .sw_req_addr(sw_req_addr), .sw_req_data(sw_req_data), .sw_done_tgl(sw_done_tgl),
  .dev_cyc(dev_cyc), .n_hit(mem_n_hit), .n_miss(mem_n_miss), .bootmap_o(bootmap_o), .hwenable_o(hwenable_o), .dosext_o(dosext_o));
assign rom_valid_o = rom_valid;

assign we     = mir_drv || (wr_cyc && fwd);
assign dout   = mir_drv ? mir_d : d_out;
assign addr   = mir_drv ? mir_a : (bus_cyc && fwd) ? a_out[15:0] : 16'h0000;
assign e_flag = e_out;

// ---- the 6510's port ----------------------------------------------------------------------------------------------------------------------
wire access_io = (a_out[15:1] == 15'd0) && (!bus_cyc || a_out[23:16] == 8'd0);       // a_out, not addr: addr is 0 for a cycle that does not go to the C64 bus
wire sel_pdir  = !a_out[0];
wire [7:0] port_rdata;

scpu_pport #(.FALL_OFF_CYCLES(FALL_OFF_CYCLES)) pport (
  .clk(clk), .clk2x(clk2x), .reset(reset), .g_edge(g_edge), .run2(run2), .clk_phi(clk_phi), .phi2_n(phi2_n),
  .access_io(access_io && bus_cyc), .sel_pdir(sel_pdir), .we(wr_cyc), .wdata(d_out), .pin(pin),
  .rdata(port_rdata), .pout(pout), .pout3_q(pout3_q),
  .st_pdir(st_pdir), .st_pout(st_pout), .st_pread(st_pread),
  .dp_we(dp_we), .dp_hi(dp_hi), .dp_data(dp_data), .dp_restore(dp_restore)
);

// a write cycle shows the CPU's own byte, a read of $00 / $01 the port, everything else the byte the bus left (the 6510 wrapper's din_early)
assign d_in      = wr_cyc ? d_out : access_io ? port_rdata : rd_mem ? mem_q : rd_int ? int_q : din_r;
assign din_r_o   = din_r;
assign dout_early = dout;
assign dout_late  = 1'b0;

// NMI taken: the vector pull of $FFFA (the keyboard's RESTORE toggle clears on it)
assign nmi_ack = !vpb && (a_out[15:0] == 16'hFFFA || a_out[15:0] == 16'hFFEA);   // emulation $FFFA, native $FFEA

// the opcode fetch is the state bus's SYNC and PC
reg [15:0] pc_q = 16'd0;
wire       fetch = vda && vpa;
always @(posedge clk2x) if (core_ce && rdy_in && fetch) pc_q <= a_out[15:0];
assign pc      = pc_q;
assign st_sync = fetch;
assign st_intg = 1'b0;
assign {st_a, st_x, st_y, st_s, st_p} = 40'd0;
assign st_nx   = 40'd0;
assign sb_cur  = 209'd0;

endmodule
