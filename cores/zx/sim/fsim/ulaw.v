// T119 5.15: the ULA with the ties zx_core.v gives it (mZX 1, no ULA+/Timex, wide / overscan 0), alone, for a synthesis of a few seconds.
// m128 is a port here (zx_core drives it from a register), so one netlist serves both models.
`timescale 1ns / 1ps
module ulaw (
    input  wire clk, input wire rst, input wire ce_n, input wire ce_p, input wire m128,
    output wire hblank, output wire hsync, output wire vblank, output wire vsync, output wire nint, output wire [3:0] igrb
);
    wire I, R, G, B;
    ULA ula (
        .reset(rst), .clk_sys(clk), .ce_7mp(ce_p), .ce_7mn(ce_n), .ce_cpu_sp(), .ce_cpu_sn(),
        .addr(16'd0), .din(8'd0), .nMREQ(1'b1), .nIORQ(1'b1), .nRFSH(1'b1), .nRD(1'b1), .nWR(1'b1), .nINT(nint),
        .vram_addr(), .vram_dout(8'd0), .port_ff(),
        .ulap_avail(1'b0), .ulap_sel(), .ulap_dout(), .ulap_ena(), .ulap_mono(), .ulap_color(),
        .tmx_avail(1'b0), .mode512(),
        .snow_ena(1'b0), .mZX(1'b1), .m128(m128), .scorp_tim(1'b0), .page_scr(1'b0), .page_ram(3'd0),
        .border_color(3'd0), .wide(2'b00), .overscan(2'b00),
        .HSync(hsync), .VSync(vsync), .HBlank(hblank), .VBlank(vblank), .I(I), .R(R), .G(G), .B(B)
    );
    assign igrb = {I, G, R, B};
endmodule
