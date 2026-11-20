#!/usr/bin/env python3
"""Derives cores/ti99/layer2_ti99.v from cores/vc20/layer2_vc20.v: same ports (the partition's edge is fixed), the VC-20 core, the 1541 and the
IEC bus replaced by ti99_core, everything the TI does not use at the idle values the host's decoupler holds.
Usage: python3 gen_layer2_ti99.py cores/vc20/layer2_vc20.v cores/ti99/layer2_ti99.v     Each edit must match exactly once."""
import re, sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()

def once(pat, repl, flags=re.S):
    global t
    n = len(re.findall(pat, t, flags))
    if n != 1: sys.exit("edit %r matched %d times (want 1); nothing written" % (pat[:50], n))
    t = re.sub(pat, lambda m: repl, t, count=1, flags=flags)

# header comment and module name
once(r"^.*?`timescale 1ns / 1ps\n",
"""// The TI-99/4A core as a layer-2 module of the TRX64 Ultimate DFX design: Mega99's mainboard (zeldin/Mega99, LGPL-3.0, trimmed; see rtl/NOTICE) behind the
// same ports as layer2.v (the partition's edge is fixed, T79.1). Derived from layer2_vc20.v by cores/ti99/gen_layer2_ti99.py: the VIC-20, the 1541 and the IEC
// bus are gone, what this module does not use sits at the idle value the host's decoupler holds. The picture goes into the frame buffer of the host's pixel
// layer through the general DDR2 port e_* (ti99_pixw); pix_* is only the idle raster the host's scaler locks to. Built only with TRX_CORE=ti99.
`timescale 1ns / 1ps
""")
once(r"module layer2_vc20 #", "module layer2_ti99 #")

# the body: from the first wire declaration to the idle-values comment
body = """    wire ti_halted, ti_running;

    // the machine: Mega99's mainboard and the glue are in cores/ti99/rtl. Every pause stops the CPU, GROM, sound and peripheral clock enables (clkgen_nco.v):
    // the app's C64_STOP (app_stop), the JTAG pause (machine_ctl[1]) and the host's halts; the VDP keeps running and the picture holds.
    ti99_core ti (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .vmode(vmode_sys), .ui_clk(ui_clk), .ui_rst(ui_rst),
        .reset_req(core_reset_req), .halt_req(c64_halt_req | machine_ctl[1] | app_stop | mc_halt_s[1]), .halted(ti_halted), .running(ti_running),
        .keys(c64_keys), .joy1(c64_joy1), .joy2(c64_joy2), .restore(c64_restore),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata_q),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .audio_l(audio_l), .audio_r(audio_r),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(cw_ack), .cw_irq(cw_irq),
        .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata), .px_ev_vsync(px_ev_vsync), .px_ev_flip(px_ev_flip),
        .build(usr_access), .ddr_cal(ddr_cal),
        .dbg_re(dbg_re && dbg_trx_sel), .dbg_addr(dbg_addr[11:0]), .j_rdata(trx_j_rdata), .j_rvalid(trx_j_rvalid),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren)
    );
    assign c64_halted = ti_halted;
    assign c64_leds = {4'b0000, ti_running};

"""
once(r"    wire c64_atn_i, c64_clk_i.*?(?=    // idle values of everything the VC-20 does not use)", body)
once(r"idle values of everything the VC-20 does not use", "idle values of everything the TI-99/4A does not use")

# the cassette block of the VC-20 and its register driving: the TI has none
once(r"    // the cassette port \(T214\).*?assign c2n_sense_o = 1'b0;\n",
"""    // the cassette port: every pin released
    assign c2n_motor_o = 1'b0;
    assign c2n_motor_en = 1'b0;
    assign c2n_write_o = 1'b1;
    assign c2n_write_en = 1'b0;
    assign c2n_read_en = 1'b0;
    assign c2n_read_o = 1'b1;
    assign c2n_sense_o = 1'b0;
    // no drive, no serial bus: the drive register windows answer "no core behind it", the bus pins stay released
    assign IEC_ATN_O = 1'b1; assign IEC_CLOCK_O = 1'b1; assign IEC_DATA_O = 1'b1;
    assign c64_atn_o = 1'b1; assign c64_clk_o = 1'b1; assign c64_data_o = 1'b1;
    assign drive_dbg = 'd0; assign drive_leds_n = 3'b111;
    assign drv_addr = 'd0; assign drv_be = 'd0; assign drv_wdata = 'd0; assign drv_we = 1'b0; assign drv_tgl = 1'b0;
    assign drv_clk_o = 1'b1; assign drv_data_o = 1'b1;
    assign drv_io_ack = 1'b0; assign drv_io_nc = 1'b1; assign drv_io_rdata = 8'd0;
    assign gcr_io_ack = 1'b0; assign gcr_io_nc = 1'b1; assign gcr_io_rdata = 8'd0;
    // the host's second memory port is not used: the app's window is the core's own (ti99_win)
    assign win_port_use = 1'b0; assign win_port_addr = 16'd0; assign win_port_rom = 1'b0; assign win_port_we = 1'b0; assign win_port_wdata = 8'd0;
    assign ram_rdata = 8'd0;
""")
open(dst, "w").write(t); print("wrote", dst)
