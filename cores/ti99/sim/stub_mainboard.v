module mainboard #(parameter vdp_clk_multiplier=1, cpu_clk_multiplier=1, INCLUDE_PCODE=1, USE_GROM_DMA=1, CROM_BANKS=64, INCLUDE_VSP=1, INCLUDE_TIPI=1, NUM_GROMS=8, audio_bits=16)
 (input clk, input ext_reset, input [1:0] clk_mode, input clk_hold, output sys_reset, input reset_9900, input reset_9901, input reset_9918, input reset_9919, input reset_5200, input reset_peb, input reset_grom,
  input cpu_turbo, output vdp_clk_en, output vga_clk_en, output overlay_clk_en, output clk_3mhz_en, input enable_ram32k, input enable_fdc, input enable_vsp, input enable_tipi, input enable_pcode, input enable_1kscratch, input swap_joysticks, input [0:3] tipi_crubase,
  output vdp_hsync, output vdp_vsync, output vdp_cburst, output [0:3] vdp_color, output vdp_color_en, output vdp_extvideo,
  input [0:15] audio_in, output [0:15] audio_out, input [0:47] key_state, input alpha_state, input [0:4] joy1, input [0:4] joy2, input [0:47] synth_key_state, input synth_keys_enabled,
  output cs1_cntrl, output cs2_cntrl, output audio_gate, output mag_out, output [1:3] drive_activity,
  input tipi_clk, input tipi_rt, input tipi_le, output tipi_reset, input tipi_dout, output tipi_din, input tipi_dc,
  output [0:15] debug_pc, output [0:15] debug_st, output [0:15] debug_wp, output [0:15] debug_ir, output [0:13] debug_vdp_addr, output [0:15] debug_grom_addr,
  input [0:23] wb_adr_i, input [0:7] wb_dat_i, output reg [0:7] wb_dat_o, input wb_we_i, input [0:0] wb_sel_i, input wb_stb_i, output reg wb_ack_o, input wb_cyc_i,
  output [0:20] dma_adr_o, output [0:31] dma_dat_o, input [0:31] dma_dat_i, output dma_we_o, output [0:3] dma_sel_o, output dma_stb_o, input dma_ack_i, output dma_cyc_o);
 assign vdp_clk_en = 0; assign vdp_color = 0; assign vdp_color_en = 0; assign vdp_vsync = 0; assign audio_out = 0;
endmodule
